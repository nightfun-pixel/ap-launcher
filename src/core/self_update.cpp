#include "core/self_update.h"

#include <windows.h>

#include <string>
#include <vector>

#include "core/config.h"
#include "core/download.h"
#include "core/fileio.h"
#include "core/http.h"
#include "core/log.h"
#include "core/manifest.h"
#include "core/sha256.h"
#include "core/text.h"

namespace blueward {

namespace {

bool CopyWithVerify(const std::wstring& from, const std::wstring& to, std::string& error) {
    if (!CopyFileAtomic(from, to, error)) return false;
    std::string a, b;
    if (!Sha256File(from, a) || !Sha256File(to, b) || a != b) {
        error = "the copied launcher does not match the downloaded file";
        RemoveFile(to);
        return false;
    }
    return true;
}

bool RemoveWithRetries(const std::wstring& path) {
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (!FileExists(path)) return true;
        if (RemoveFile(path)) return true;
        Sleep(200);
    }
    return !FileExists(path);
}

bool RunResourceManifest(const std::wstring& exe, std::string& xml, std::string& error) {
    HMODULE module = LoadLibraryExW(exe.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if (!module) {
        error = "cannot open the executable as data (Windows " + std::to_string(GetLastError()) + ")";
        return false;
    }
    const HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(1), MAKEINTRESOURCEW(24));
    if (!resource) {
        FreeLibrary(module);
        xml.clear();
        return true;  // absent manifest means asInvoker
    }
    const DWORD size = SizeofResource(module, resource);
    const HGLOBAL loaded = LoadResource(module, resource);
    const void* data = loaded ? LockResource(loaded) : nullptr;
    if (!data || size == 0) {
        FreeLibrary(module);
        error = "the manifest resource is empty";
        return false;
    }
    xml.assign(static_cast<const char*>(data), size);
    FreeLibrary(module);
    return true;
}

}  // namespace

bool ShouldUpdate(std::string_view currentVersion, const LauncherInfo& latest) {
    return CompareVersions(latest.version, currentVersion) > 0;
}

std::wstring UpdateDir() { return DefaultStateDir() + L"\\update"; }

std::wstring UpdateFileName(std::string_view version) {
    std::string safe;
    for (const char c : version) {
        const bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '.' || c == '-' || c == '_';
        safe.push_back(ok ? c : '_');
    }
    if (safe.empty()) safe = "new";
    return L"BLUEWARD-" + Widen(safe) + L".exe";
}

bool DownloadUpdate(const UpdateInfo& info, const std::wstring& updateDir, HttpClient& http, const ProgressFn& progress,
                    std::atomic<bool>& cancel, std::wstring& newExe, std::string& error) {
    error.clear();
    newExe.clear();
    if (!IsAllowedDownloadUrl(info.url, error)) return false;
    if (!MakeDirs(updateDir)) {
        error = "cannot create the update folder";
        return false;
    }
    const std::wstring part = updateDir + L"\\" + UpdateFileName(info.version) + L".part";
    const DownloadResult result = DownloadToPart(http, info.url, part, info.size, info.sha256, progress, cancel);
    if (!result.ok) {
        error = result.error;
        return false;
    }
    const std::wstring destination = updateDir + L"\\" + UpdateFileName(info.version);
    RemoveFile(destination);
    if (!MoveFileExW(part.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        error = "cannot finalize the downloaded launcher (Windows " + std::to_string(GetLastError()) + ")";
        return false;
    }
    newExe = destination;
    return true;
}

bool ReadExecutionLevel(const std::wstring& exe, std::wstring& level, std::string& error) {
    level = L"asInvoker";
    error.clear();
    std::string xml;
    if (!RunResourceManifest(exe, xml, error)) return false;
    if (xml.empty()) return true;
    const std::string needle = "requestedExecutionLevel";
    const std::size_t at = xml.find(needle);
    if (at == std::string::npos) return true;
    const std::size_t levelAt = xml.find("level", at);
    if (levelAt == std::string::npos) return true;
    const std::size_t quote = xml.find('"', xml.find('=', levelAt));
    if (quote == std::string::npos) return true;
    const std::size_t end = xml.find('"', quote + 1);
    if (end == std::string::npos) return true;
    level = Widen(xml.substr(quote + 1, end - quote - 1));
    return true;
}

bool IsProcessElevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    TOKEN_ELEVATION elevation{};
    DWORD size = sizeof elevation;
    const bool elevated = GetTokenInformation(token, TokenElevation, &elevation, sizeof elevation, &size) != FALSE && elevation.TokenIsElevated != 0;
    CloseHandle(token);
    return elevated;
}

bool SpawnDetached(const std::wstring& exe, const std::wstring& arguments, std::string& error) {
    error.clear();
    if (!FileExists(exe)) {
        error = "the new launcher is missing";
        return false;
    }
    std::wstring command = arguments;
    if (command.empty()) command = L"\"" + exe + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof startup;
    PROCESS_INFORMATION process{};
    const std::wstring working = ParentDir(exe);
    if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_UNICODE_ENVIRONMENT, nullptr, working.c_str(), &startup,
                        &process)) {
        error = "cannot start the new launcher (Windows " + std::to_string(GetLastError()) + ")";
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

bool ApplyUpdateFiles(const std::wstring& currentExe, const std::wstring& downloadedExe, const std::wstring& arguments,
                      const HandoffCallbacks& callbacks, std::string& error) {
    error.clear();
    if (!FileExists(currentExe)) {
        error = "the current launcher is missing";
        return false;
    }
    if (!FileExists(downloadedExe)) {
        error = "the downloaded launcher is missing";
        return false;
    }
    const std::wstring old = currentExe + L".old";
    const std::wstring fresh = currentExe + L".new";
    RemoveWithRetries(old);
    RemoveWithRetries(fresh);
    if (!CopyWithVerify(downloadedExe, fresh, error)) return false;
    if (!MoveFileExW(currentExe.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        RemoveFile(fresh);
        error = "cannot rename the running launcher (Windows " + std::to_string(GetLastError()) + ")";
        return false;
    }
    if (!MoveFileExW(fresh.c_str(), currentExe.c_str(), MOVEFILE_WRITE_THROUGH)) {
        MoveFileExW(old.c_str(), currentExe.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        RemoveFile(fresh);
        error = "cannot place the new launcher (Windows " + std::to_string(GetLastError()) + ")";
        return false;
    }
    if (callbacks.releaseMutex) callbacks.releaseMutex();
    const bool spawned = callbacks.spawn ? callbacks.spawn(currentExe) : SpawnDetached(currentExe, arguments, error);
    if (!spawned) {
        if (callbacks.reacquireMutex) callbacks.reacquireMutex();
        MoveFileExW(currentExe.c_str(), fresh.c_str(), MOVEFILE_REPLACE_EXISTING);
        MoveFileExW(old.c_str(), currentExe.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        RemoveFile(fresh);
        if (error.empty()) error = "cannot start the new launcher";
        return false;
    }
    RemoveFile(downloadedExe);
    return true;
}

bool DeleteOldOnStart(const std::wstring& currentExe) {
    const bool old = RemoveWithRetries(currentExe + L".old");
    const bool fresh = RemoveWithRetries(currentExe + L".new");
    return old && fresh;
}

}  // namespace blueward
