#pragma once
// Self-update: download the new BLUEWARD.exe, verify it, compare the requested execution level,
// then hand off in-process: current -> .old, new -> current, release the mutex, start the new
// image with the original arguments, exit. On the next start .old/.new are cleaned up.

#include <atomic>
#include <functional>
#include <string>

#include "core/download.h"
#include "core/manifest.h"

namespace blueward {

struct UpdateInfo {
    std::string version;
    std::string url;
    std::string sha256;
    std::uint64_t size = 0;
};

struct HandoffCallbacks {
    std::function<void()> releaseMutex;          // called after the new file is in place
    std::function<bool()> reacquireMutex;        // called if a later step fails
    std::function<bool(const std::wstring&)> spawn;  // starts the new executable; default CreateProcessW
};

bool ShouldUpdate(std::string_view currentVersion, const LauncherInfo& latest);

std::wstring UpdateDir();
std::wstring UpdateFileName(std::string_view version);  // BLUEWARD-<version>.exe

bool DownloadUpdate(const UpdateInfo& info, const std::wstring& updateDir, class HttpClient& http, const ProgressFn& progress,
                    std::atomic<bool>& cancel, std::wstring& newExe, std::string& error);

// Reads RT_MANIFEST as data (never executes the image) and returns requestedExecutionLevel.
// Absent or unreadable means asInvoker.
bool ReadExecutionLevel(const std::wstring& exe, std::wstring& level, std::string& error);
bool IsProcessElevated();

// File handoff without requiring a running parent. `arguments` is the original command line.
bool ApplyUpdateFiles(const std::wstring& currentExe, const std::wstring& downloadedExe, const std::wstring& arguments,
                      const HandoffCallbacks& callbacks, std::string& error);

bool SpawnDetached(const std::wstring& exe, const std::wstring& arguments, std::string& error);
// Removes BLUEWARD.exe.old and BLUEWARD.exe.new with three 200 ms retries.
bool DeleteOldOnStart(const std::wstring& currentExe);

}  // namespace blueward
