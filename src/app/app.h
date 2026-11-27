#pragma once
// The launcher's state and the only long-running work: install, restore and self-update. Each job
// runs on one worker thread; the UI thread only reads atomics and the view model.

#include <windows.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include "core/config.h"
#include "core/feed.h"
#include "core/installer.h"
#include "core/manifest.h"
#include "ui/pages.h"

namespace blueward {

class LauncherWindow;

struct JobResult {
    bool ok = false;
    bool quit = false;          // the self-update installed a new exe and asks this one to exit
    bool refresh = true;
    bool noPatch = false;       // the local revision has no manifest; not an error, and no write happened
    std::string code;
    std::wstring message;       // the success line or the error
};

class App {
public:
    App() = default;
    App(const App&) = delete;
    App& operator=(const App&) = delete;
    ~App();

    bool Init(HINSTANCE instance);
    void Shutdown();

    void AttachWindow(LauncherWindow* window) { window_ = window; }
    LauncherWindow* Window() const { return window_; }

    const Config& Settings() const { return config_; }
    Config& Settings() { return config_; }
    const ui::AppView& View() const { return view_; }

    void RefreshState();
    void StartInstall();
    void StartRestore();
    void StartSelfUpdate();
    void CheckFeed(bool announce);
    void CancelJob();
    bool Busy() const { return busy_; }

    void PollProgress();  // UI thread: copy the worker's counters into the view
    void TakeJobResult(JobResult* result);
    void TakeCheckResult();  // UI thread: read the feed check's fields

    void PickGameFolder();
    void OpenGameFolder();
    void OpenLog();
    void OpenUrl(const std::wstring& url);
    void SetGamePath(const std::wstring& path);
    void SetPage(ui::Page page);
    void ToggleVideo();
    void ToggleCheckUpdates();
    void SaveWindowPlacement(HWND hwnd);
    void RecoverUnfinished();
    void SetMutexHooks(std::function<void()> releaseMutex, std::function<bool()> reacquireMutex);

    const std::wstring& ExePath() const { return exePath_; }
    const std::wstring& ExeDirPath() const { return exeDir_; }
    const std::wstring& StateDir() const { return stateDir_; }
    const std::wstring& BackupsDir() const { return backupsDir_; }
    const std::wstring& CacheDir() const { return cacheDir_; }
    const std::wstring& StagingDir() const { return stagingDir_; }
    const std::wstring& DownloadsDir() const { return downloadsDir_; }
    const std::wstring& LogPath() const { return logPath_; }

private:
    void JoinWorker();
    void SaveSettings();
    void SetError(const std::wstring& text);
    void ClearError();

    HINSTANCE instance_ = nullptr;
    LauncherWindow* window_ = nullptr;
    Config config_;
    ui::AppView view_;
    std::wstring exePath_, exeDir_, stateDir_, backupsDir_, cacheDir_, logsDir_, logPath_, stagingDir_, downloadsDir_;
    std::function<void()> releaseMutex_;
    std::function<bool()> reacquireMutex_;
    std::wstring lastGateDetail_;

    std::thread worker_;
    std::thread checkThread_;
    std::atomic<bool> busy_{false};
    std::atomic<bool> cancel_{false};
    std::atomic<int> phase_{0};
    std::atomic<unsigned long long> done_{0}, total_{0};

    // Feed check result, written by the checker thread, read after kMsgCheckDone.
    std::atomic<bool> checkBusy_{false};
    std::mutex checkLock_;
    bool announceCheck_ = false;
    bool checkOk_ = false;
    FeedCheck checkFeed_;
    std::wstring checkError_;
};

}  // namespace blueward
