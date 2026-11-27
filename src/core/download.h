#pragma once
// Resumable download to a `.part` file: validates Content-Range, restarts on a server that ignores
// Range, verifies size then SHA-256, deletes a damaged file, and keeps a cancelled part for resume.

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

namespace blueward {

class HttpClient;

struct DownloadResult {
    bool ok = false;
    bool cancelled = false;
    bool resumable = false;
    bool alreadyPresent = false;
    int httpStatus = 0;
    std::string error;
};

// phase is "downloading" or "verifying". Return false to cancel.
using ProgressFn = std::function<bool(const char* phase, std::uint64_t done, std::uint64_t total)>;

// Fetches at most `maxBytes` into `body`. Status must be 200.
bool FetchText(HttpClient& http, const std::string& url, std::size_t maxBytes, std::string& body, std::string& error);

DownloadResult DownloadToPart(HttpClient& http, const std::string& url, const std::wstring& partPath, std::uint64_t expectedSize,
                              const std::string& expectedSha256, const ProgressFn& progress, std::atomic<bool>& cancel);

}  // namespace blueward
