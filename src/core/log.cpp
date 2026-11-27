#include "core/log.h"

#include <windows.h>

#include <mutex>
#include <string>

#include "core/fileio.h"
#include "core/text.h"

namespace blueward {

namespace {

std::mutex g_lock;
HANDLE g_file = INVALID_HANDLE_VALUE;
std::wstring g_path;
std::uint64_t g_size = 0;
std::uint64_t g_cap = 4ull << 20;

const wchar_t* LevelName(LogLevel level) {
    switch (level) {
        case LogLevel::Warning: return L"WARN ";
        case LogLevel::Error: return L"ERROR";
        default: return L"INFO ";
    }
}

std::wstring Timestamp() {
    SYSTEMTIME now{};
    GetSystemTime(&now);
    wchar_t text[40];
    swprintf(text, 40, L"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond,
             now.wMilliseconds);
    return text;
}

std::wstring RotatedPath(const std::wstring& path) {
    const std::wstring name = BaseName(path);
    if (name == L"launcher.log") return ParentDir(path) + L"\\launcher.1.log";
    const std::size_t dot = name.find_last_of(L'.');
    if (dot == std::wstring::npos) return path + L".1";
    return ParentDir(path) + L"\\" + name.substr(0, dot) + L".1" + name.substr(dot);
}

std::wstring ShortenUserProfile(std::wstring text) {
    wchar_t profile[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH);
    if (length > 0 && length < MAX_PATH) {
        const std::wstring prefix(profile, length);
        std::size_t at = text.find(prefix);
        while (at != std::wstring::npos) {
            text.replace(at, prefix.size(), L"%USERPROFILE%");
            at = text.find(prefix, at + 14);
        }
    }
    return text;
}

void CloseLocked() {
    if (g_file != INVALID_HANDLE_VALUE) {
        CloseHandle(g_file);
        g_file = INVALID_HANDLE_VALUE;
    }
    g_size = 0;
}

bool OpenLocked(const std::wstring& path) {
    MakeDirs(ParentDir(path));
    const std::wstring rotated = RotatedPath(path);
    if (FileExists(path) && FileSize(path) >= g_cap) {
        DeleteFileW(rotated.c_str());
        MoveFileExW(path.c_str(), rotated.c_str(), MOVEFILE_REPLACE_EXISTING);
    }
    HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    GetFileSizeEx(file, &size);
    g_file = file;
    g_path = path;
    g_size = static_cast<std::uint64_t>(size.QuadPart);
    return true;
}

void RotateLocked() {
    if (g_file == INVALID_HANDLE_VALUE) return;
    CloseLocked();
    const std::wstring rotated = RotatedPath(g_path);
    DeleteFileW(rotated.c_str());
    MoveFileExW(g_path.c_str(), rotated.c_str(), MOVEFILE_REPLACE_EXISTING);
    OpenLocked(g_path);
}

void AppendLocked(const std::wstring& line) {
    if (g_file == INVALID_HANDLE_VALUE) return;
    std::string utf8 = Narrow(line) + "\n";
    if (g_size + utf8.size() > g_cap && g_size > 0) {
        RotateLocked();
        if (g_file == INVALID_HANDLE_VALUE) return;
    }
    DWORD written = 0;
    if (!WriteFile(g_file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr) || written != utf8.size()) {
        CloseLocked();  // a write failure disables file logging for this session
        return;
    }
    FlushFileBuffers(g_file);
    g_size += written;
}

}  // namespace

bool LogOpen(const std::wstring& path, std::uint64_t capBytes) {
    std::lock_guard lock(g_lock);
    CloseLocked();
    g_cap = capBytes == 0 ? (4ull << 20) : capBytes;
    return OpenLocked(path);
}

void LogClose() {
    std::lock_guard lock(g_lock);
    CloseLocked();
    g_path.clear();
}

bool LogIsOpen() {
    std::lock_guard lock(g_lock);
    return g_file != INVALID_HANDLE_VALUE;
}

std::wstring LogFilePath() {
    std::lock_guard lock(g_lock);
    return g_path;
}

void LogWrite(LogLevel level, const std::wstring& text) {
    std::lock_guard lock(g_lock);
    if (g_file == INVALID_HANDLE_VALUE) return;
    const std::wstring line = Timestamp() + L" " + LevelName(level) + L" " + ShortenUserProfile(SanitizeLogText(text));
    AppendLocked(line);
}

void LogInfo(const std::wstring& text) { LogWrite(LogLevel::Info, text); }
void LogWarn(const std::wstring& text) { LogWrite(LogLevel::Warning, text); }
void LogError(const std::wstring& text) { LogWrite(LogLevel::Error, text); }

std::wstring SanitizeLogText(const std::wstring& text) {
    std::wstring out = text;
    for (wchar_t& c : out)
        if (c < 0x20 || c == 0x7F) c = L' ';
    // Remove query strings from URLs: a presigned link carries its signature there.
    std::size_t search = 0;
    while (search < out.size()) {
        const std::size_t https = out.find(L"https://", search);
        const std::size_t http = out.find(L"http://", search);
        if (https == std::wstring::npos && http == std::wstring::npos) break;
        const std::size_t start = https == std::wstring::npos ? http : (http == std::wstring::npos ? https : (std::min)(https, http));
        std::size_t end = out.find_first_of(L" \t", start);
        if (end == std::wstring::npos) end = out.size();
        const std::size_t query = out.find(L'?', start);
        if (query != std::wstring::npos && query < end) {
            out.erase(query, end - query);
            search = query;
        } else {
            search = end;
        }
    }
    return out;
}

}  // namespace blueward
