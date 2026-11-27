#include "core/download.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <vector>

#include "core/errors.h"
#include "core/fileio.h"
#include "core/http.h"
#include "core/log.h"
#include "core/sha256.h"
#include "core/text.h"

namespace blueward {

namespace {

constexpr std::uint64_t kHeadroom = 64ull << 20;

bool ParseContentRange(const std::string& value, std::uint64_t& start, std::uint64_t& total) {
    if (value.rfind("bytes ", 0) != 0) return false;
    char* end = nullptr;
    start = std::strtoull(value.c_str() + 6, &end, 10);
    if (end == value.c_str() + 6 || *end != '-') return false;
    std::strtoull(end + 1, &end, 10);
    if (*end != '/') return false;
    const char* totalText = end + 1;
    total = std::strtoull(totalText, &end, 10);
    return end != totalText && *end == '\0';
}

}  // namespace

bool FetchText(HttpClient& http, const std::string& url, std::size_t maxBytes, std::string& body, std::string& error) {
    body.clear();
    error.clear();
    for (int attempt = 0; attempt < 4; ++attempt) {
        std::string stopped;
        body.clear();
        const HttpResult result = http.Get(
            url, {{"Accept", "application/json"}},
            [&](const HttpResult& headers) {
                if (headers.status != 200) {
                    stopped = "the server answered HTTP " + std::to_string(headers.status);
                    return false;
                }
                if (headers.hasContentLength && headers.contentLength > maxBytes) {
                    stopped = "the response is larger than the limit";
                    return false;
                }
                return true;
            },
            [&](const void* data, std::size_t size) {
                if (body.size() + size > maxBytes) {
                    stopped = "the response is larger than the limit";
                    return false;
                }
                body.append(static_cast<const char*>(data), size);
                return true;
            });
        if (!stopped.empty()) {
            error = stopped;
            if (result.status == 404 || result.status == 410 || result.status == 200) return false;
        }
        if (result.completed && result.status == 200) {
            error.clear();
            return true;
        }
        if (result.status == 404 || result.status == 410) {
            error = "the server answered HTTP " + std::to_string(result.status);
            return false;
        }
        const bool retryable = (!result.completed && result.systemError != 0) || result.status == 429 || result.status >= 500;
        if (!retryable || attempt == 3) {
            if (!result.error.empty()) error = result.error;
            else if (result.status != 0) error = "the server answered HTTP " + std::to_string(result.status);
            else if (error.empty()) error = "the request failed";
            return false;
        }
        const int wait = result.retryAfterSeconds > 0 ? result.retryAfterSeconds : (1 << attempt);
        Sleep(static_cast<DWORD>((std::min)(wait, 60)) * 1000);
    }
    error = "the request failed after retries";
    return false;
}

DownloadResult DownloadToPart(HttpClient& http, const std::string& url, const std::wstring& partPath, std::uint64_t expectedSize,
                              const std::string& expectedSha256, const ProgressFn& progress, std::atomic<bool>& cancel) {
    DownloadResult result;
    std::string why;
    if (!IsAllowedUrl(url, why)) {
        result.error = "refusing " + url + ": " + why;
        return result;
    }
    MakeDirs(ParentDir(partPath));
    std::uint64_t have = FileSize(partPath);
    if (have > expectedSize) {  // not a prefix of this file
        RemoveFile(partPath);
        have = 0;
    }
    ULARGE_INTEGER freeBytes{};
    const std::wstring parent = ParentDir(partPath);
    if (GetDiskFreeSpaceExW(parent.c_str(), &freeBytes, nullptr, nullptr) && expectedSize > have &&
        freeBytes.QuadPart < (expectedSize - have) + kHeadroom) {
        result.error = "not enough free disk space: " + std::to_string(((expectedSize - have) + kHeadroom) >> 20) + " MiB needed";
        return result;
    }

    using Clock = std::chrono::steady_clock;
    Clock::time_point lastReport{};
    auto report = [&](const char* phase, std::uint64_t done, std::uint64_t total, bool force) {
        if (!progress) return true;
        const auto now = Clock::now();
        const bool small = total <= (4u << 20);  // small files: report every chunk, so a callback can cancel at once
        if (!force && !small && now - lastReport < std::chrono::milliseconds(250)) return true;
        lastReport = now;
        return progress(phase, done, total);
    };
    if (!report("downloading", have, expectedSize, true)) {
        result.cancelled = result.resumable = true;
        result.error = "cancelled";
        return result;
    }

    for (int round = 0; round < 2; ++round) {
        HANDLE file = CreateFileW(partPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            result.error = "cannot open the partial file (Windows " + std::to_string(GetLastError()) + ")";
            return result;
        }
        auto seek = [&](std::uint64_t to) {
            LARGE_INTEGER at{};
            at.QuadPart = static_cast<LONGLONG>(to);
            SetFilePointerEx(file, at, nullptr, FILE_BEGIN);
            SetEndOfFile(file);
        };
        seek(have);

        std::vector<HttpHeader> headers;
        if (have > 0) headers.push_back({"Range", "bytes=" + std::to_string(have) + "-"});

        std::string failure;
        bool retryFromZero = false, cancelled = false, alreadyWhole = false, localFault = false;
        std::uint64_t written = have;
        const HttpResult response = http.Get(
            url, headers,
            [&](const HttpResult& h) {
                result.httpStatus = h.status;
                if (cancel.load()) {
                    cancelled = true;
                    return false;
                }
                if (h.status == 206) {
                    std::uint64_t start = 0, total = 0;
                    if (have == 0 || !ParseContentRange(h.contentRange, start, total) || start != have || total != expectedSize) {
                        failure = "the server answered with a byte range that does not match the request";
                        return false;
                    }
                    return true;
                }
                if (h.status == 200) {
                    if (h.hasContentLength && h.contentLength != expectedSize) {
                        failure = "the server sends " + std::to_string(h.contentLength) + " bytes, expected " + std::to_string(expectedSize);
                        return false;
                    }
                    if (have > 0) {  // the server ignored Range: the whole file comes again
                        seek(0);
                        written = 0;
                    }
                    return true;
                }
                if (h.status == 416) {
                    if (have == expectedSize)
                        alreadyWhole = true;
                    else
                        retryFromZero = true;
                    return false;
                }
                failure = "the server answered HTTP " + std::to_string(h.status);
                return false;
            },
            [&](const void* data, std::size_t size) {
                if (cancel.load()) {
                    cancelled = true;
                    return false;
                }
                if (written + size > expectedSize) {
                    failure = "the server sent more data than the manifest says";
                    return false;
                }
                DWORD put = 0;
                if (!WriteFile(file, data, static_cast<DWORD>(size), &put, nullptr) || put != size) {
                    failure = "cannot write the partial file (Windows " + std::to_string(GetLastError()) + ")";
                    localFault = true;
                    return false;
                }
                written += size;
                if (!report("downloading", written, expectedSize, false)) {
                    cancelled = true;
                    return false;
                }
                return true;
            });
        FlushFileBuffers(file);
        CloseHandle(file);
        if (FileSize(partPath) == 0) RemoveFile(partPath);

        if (retryFromZero && round == 0) {
            RemoveFile(partPath);
            have = 0;
            continue;
        }
        if (cancelled) {
            result.cancelled = result.resumable = true;
            result.error = "cancelled";
            return result;
        }
        if (!alreadyWhole) {
            if (!failure.empty()) {
                result.error = failure;
                result.resumable = FileSize(partPath) > 0 && result.httpStatus != 416;
                break;
            }
            if (!response.completed) {
                result.error = response.error;
                result.resumable = FileSize(partPath) > 0;
                break;
            }
        }
        break;
    }
    if (!result.error.empty()) return result;

    if (FileSize(partPath) != expectedSize) {
        result.error = "the download is incomplete (" + std::to_string(FileSize(partPath)) + " of " + std::to_string(expectedSize) + " bytes)";
        result.resumable = true;
        return result;
    }
    if (!report("downloading", expectedSize, expectedSize, true)) {
        result.cancelled = result.resumable = true;
        result.error = "cancelled";
        return result;
    }
    if (!report("verifying", 0, expectedSize, true)) {
        result.cancelled = result.resumable = true;
        result.error = "cancelled";
        return result;
    }
    bool aborted = false;
    std::string hash, hashError;
    if (!Sha256File(
            partPath, hash,
            [&](std::uint64_t done, std::uint64_t total) {
                const bool keepGoing = report("verifying", done, total, false);
                aborted = !keepGoing;
                return keepGoing;
            },
            hashError)) {
        result.cancelled = aborted;
        result.resumable = aborted;
        result.error = aborted ? "cancelled" : "the downloaded file could not be read back";
        return result;
    }
    if (hash != expectedSha256) {
        RemoveFile(partPath);  // resuming a damaged file would repeat the damage
        result.error = "SHA-256 mismatch: the file is damaged or the manifest is wrong; the download was deleted";
        return result;
    }
    result.ok = true;
    return result;
}

}  // namespace blueward
