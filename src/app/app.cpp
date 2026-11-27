#include "app/app.h"

#include <shobjidl.h>
#include <shlobj.h>
#include <shellapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstring>

#include "app/window.h"
#include "core/download.h"
#include "core/fileio.h"
#include "core/gamepath.h"
#include "core/http.h"
#include "core/log.h"
#include "core/messages.h"
#include "core/self_update.h"
#include "core/sha256.h"
#include "core/text.h"
#include "core/version.h"

using Microsoft::WRL::ComPtr;

namespace blueward {

namespace {

std::wstring PhaseStatus(int phase) {
    switch (static_cast<InstallPhase>(phase)) {
        case InstallPhase::Checking: return msg::FromUtf8(msg::kPreflight);
        case InstallPhase::Downloading: return msg::FromUtf8(msg::kDownloading);
        case InstallPhase::Verifying: return msg::FromUtf8(msg::kVerifyingArchive);
        case InstallPhase::Extracting: return msg::FromUtf8(msg::kExtracting);
        case InstallPhase::Installing: return msg::FromUtf8(msg::kApplying);
        case InstallPhase::Done: return msg::FromUtf8(msg::kDone);
        default: return {};
    }
}

std::wstring GateMessage(const InstallPlan& plan, const std::wstring& detail) {
    return msg::RevisionUpdate(Widen(plan.resourceRevision)) + L" " + detail;
}

bool OpenFolderSelect(const std::wstring& path) {
    PIDLIST_ABSOLUTE item = ILCreateFromPathW(path.c_str());
    if (!item) return false;
    const HRESULT hr = SHOpenFolderAndSelectItems(item, 0, nullptr, 0);
    ILFree(item);
    return SUCCEEDED(hr);
}

}  // namespace

App::~App() { Shutdown(); }

bool App::Init(HINSTANCE instance) {
    instance_ = instance;
    exePath_ = ExePath();
    exeDir_ = ParentDir(exePath_);
    stateDir_ = DefaultStateDir();
    logsDir_ = DefaultLogsDir();
    backupsDir_ = DefaultBackupsDir();
    cacheDir_ = DefaultCacheDir();
    stagingDir_ = DefaultStagingDir();
    downloadsDir_ = DefaultDownloadsDir();
    logPath_ = logsDir_ + L"\\launcher.log";
    MakeDirs(logsDir_);
    MakeDirs(backupsDir_);
    MakeDirs(cacheDir_);
    MakeDirs(stagingDir_);
    MakeDirs(downloadsDir_);
    LogOpen(logPath_);
    LogInfo(L"BLUEWARD " + std::wstring(kBluewardVersionW) + L" запущен: " + exePath_);
    DeleteOldOnStart(exePath_);

    config_ = LoadConfig(DefaultConfigPath());
    if (config_.gamePath.empty() || !IsGameDir(config_.gamePath)) {
        const std::wstring detected = AutoDetectGameDir(CommonGameRoots(exeDir_));
        if (!detected.empty()) {
            config_.gamePath = detected;
            LogInfo(L"папка игры найдена автоматически: " + detected);
            SaveSettings();
        }
    }
    view_.launcherVersion = kBluewardVersionW;
    view_.page = ui::Page::Main;
    RefreshState();
    const std::wstring gameDir = config_.gamePath;
    if (!gameDir.empty()) {
        std::wstring journal;
        if (FindUnfinishedJournal(gameDir, backupsDir_, journal)) {
            view_.unfinished = true;
            view_.status = msg::FromUtf8(msg::kUnfinished);
            LogWarn(L"найден незавершённый журнал: " + journal);
        }
    }
    return true;
}

void App::Shutdown() {
    cancel_ = true;
    if (worker_.joinable()) worker_.join();
    if (checkThread_.joinable()) checkThread_.join();
    if (logPath_.empty()) return;
    LogInfo(L"BLUEWARD закрыт");
    LogClose();
    logPath_.clear();
}

void App::SetMutexHooks(std::function<void()> releaseMutex, std::function<bool()> reacquireMutex) {
    releaseMutex_ = std::move(releaseMutex);
    reacquireMutex_ = std::move(reacquireMutex);
}

void App::RefreshState() {
    const ui::Page page = view_.page;
    view_.page = page;
    view_.hasGame = !config_.gamePath.empty() && IsGameDir(config_.gamePath);
    view_.gamePath = config_.gamePath;
    view_.gameVersion = view_.hasGame ? ReadGameVersion(config_.gamePath) : std::wstring();
    const std::wstring revision = view_.hasGame ? LocalResourceRevision(config_.gamePath) : std::wstring();
    if (revision != view_.resourceRevision) {
        view_.feedChecked = false;
        view_.feedChecking = false;
        view_.patchAvailable = false;
        view_.noPatch = false;
        view_.requiredRevision.clear();
        view_.latestPatch.clear();
    }
    view_.resourceRevision = revision;
    std::string installed;
    std::wstring installedAt;
    view_.installed = InstalledVersion(stateDir_, installed, &installedAt);
    view_.installedVersion = Widen(installed);
    view_.installedAt = installedAt;
    view_.launcherVersion = kBluewardVersionW;
    view_.video = config_.video;
    view_.checkUpdates = config_.checkUpdates;
    view_.telegram = TelegramEnabled(config_.telegramUrl) ? Widen(config_.telegramUrl) : std::wstring();
    const LocalGate gate = EvaluateLocalState(config_.gamePath, stateDir_);
    view_.localState = gate.state;
    view_.canInstall = gate.canInstall && !view_.noPatch && !(view_.feedChecked && !view_.patchAvailable);
    view_.gateDetail = gate.detail;
    view_.gateCode = Widen(gate.code);
    if (gate.detail != lastGateDetail_) {
        if (gate.state == LocalState::NeedsGameUpdate) LogWarn(L"локальная проверка: " + gate.detail);
        else if (gate.state == LocalState::Ready) LogInfo(L"локальная проверка: все пять целей совпадают");
        else if (gate.state == LocalState::Installed) LogInfo(L"локальная проверка: перевод установлен");
        lastGateDetail_ = gate.detail;
    }
    if (view_.installed && !view_.latestPatch.empty())
        view_.updateAvailable = CompareVersions(Narrow(view_.latestPatch), installed) > 0;
    else
        view_.updateAvailable = false;
    if (view_.status.empty() && !view_.error.empty()) view_.status.clear();
    if (view_.error.empty() && view_.status.empty()) {
        if (!view_.hasGame) view_.status = msg::FromUtf8(msg::kIdleNoGame);
        else if (view_.installed) view_.status = msg::FromUtf8(msg::kIdleInstalled);
        else view_.status = msg::FromUtf8(msg::kIdleReady);
    }
}

void App::SetError(const std::wstring& text) {
    view_.error = text;
    view_.errorCode.clear();
    view_.status.clear();
    LogError(text);
}

void App::ClearError() {
    view_.error.clear();
    view_.errorCode.clear();
}

void App::PollProgress() {
    if (!busy_) return;
    if (view_.job == ui::Job::Installing || view_.job == ui::Job::Checking) {
        const int phase = phase_.load();
        view_.status = PhaseStatus(phase);
        const unsigned long long done = done_.load(), total = total_.load();
        view_.progress = total > 0 ? static_cast<double>(done) / static_cast<double>(total) : 0.0;
    }
}

void App::TakeJobResult(JobResult* result) {
    if (!result) return;
    const bool quit = result->quit;
    busy_ = false;
    cancel_ = false;
    view_.job = ui::Job::None;
    view_.progress = 0.0;
    if (result->noPatch) {
        view_.feedChecked = true;
        view_.patchAvailable = false;
        view_.noPatch = true;
        view_.canInstall = false;
        view_.requiredRevision.clear();
        view_.latestPatch.clear();
        view_.updateAvailable = false;
        view_.status = result->message;
        view_.error.clear();
        view_.errorCode.clear();
    } else if (!result->message.empty()) {
        if (result->ok) {
            view_.status = result->message;
            view_.error.clear();
            view_.errorCode.clear();
        } else {
            view_.error = result->message;
            view_.errorCode = Widen(result->code);
            view_.status.clear();
        }
    }
    if (result->refresh) {
        view_.unfinished = false;
        RefreshState();
    }
    delete result;
    if (window_) window_->Invalidate();
    if (quit && window_ && window_->hwnd()) PostMessageW(window_->hwnd(), WM_CLOSE, 0, 0);
}

void App::TakeCheckResult() {
    std::lock_guard lock(checkLock_);
    view_.feedChecking = false;
    if (checkOk_) {
        view_.feedChecked = true;
        if (checkFeed_.launcherKnown) {
            view_.latestLauncher = Widen(checkFeed_.launcher.version);
            view_.launcherUpdate = ShouldUpdate(kBluewardVersion, checkFeed_.launcher);
        }
        if (!view_.hasGame || checkFeed_.localRevision != Narrow(view_.resourceRevision)) {
            view_.feedChecked = false;
            return;
        }
        if (checkFeed_.status == FeedStatus::Available) {
            view_.patchAvailable = true;
            view_.noPatch = false;
            view_.requiredRevision = Widen(checkFeed_.patchManifest.patch.resourceRevision);
            view_.latestPatch = Widen(checkFeed_.patchManifest.patch.version);
            view_.canInstall = view_.localState == LocalState::Ready || view_.localState == LocalState::Installed;
            if (view_.installed)
                view_.updateAvailable =
                    CompareVersions(checkFeed_.patchManifest.patch.version, Narrow(view_.installedVersion)) > 0;
        } else {
            view_.patchAvailable = false;
            view_.noPatch = true;
            view_.canInstall = false;
            view_.requiredRevision.clear();
            view_.latestPatch.clear();
            view_.updateAvailable = false;
            view_.status = msg::NoPatch(Widen(checkFeed_.localRevision));
        }
    } else if (announceCheck_) {
        view_.error = checkError_.empty() ? L"Не удалось проверить обновления." : checkError_;
        view_.errorCode = L"E_NET";
    }
    if (window_) window_->Invalidate();
}

void App::StartInstall() {
    if (busy_) return;
    if (!IsGameDir(config_.gamePath)) {
        SetError(msg::FromUtf8(msg::kGameInvalid));
        if (window_) window_->Invalidate();
        return;
    }
    if (view_.localState == LocalState::NeedsGameUpdate) {
        const std::wstring text = msg::FromUtf8(msg::kNeedsGameUpdate) +
                                  (view_.gateDetail.empty() ? std::wstring() : L" " + view_.gateDetail);
        view_.error = text;
        view_.errorCode = view_.gateCode.empty() ? L"E_VERSION_BEFORE" : view_.gateCode;
        view_.status = msg::FromUtf8(msg::kNeedsGameUpdate);
        LogWarn(text);
        if (window_) window_->Invalidate();
        return;
    }
    if (worker_.joinable()) worker_.join();
    busy_ = true;
    cancel_ = false;
    phase_ = static_cast<int>(InstallPhase::Checking);
    done_ = 0;
    total_ = 0;
    view_.job = ui::Job::Checking;
    view_.status = msg::FromUtf8(msg::kChecking);
    view_.progress = 0.0;
    ClearError();
    const std::wstring gameDir = config_.gamePath;
    worker_ = std::thread([this, gameDir] {
        auto* result = new JobResult();
        std::wstring message;
        std::string error;
        bool ok = false;
        WinHttpClient http;
        if (GameProcessesRunning(IsProcessRunning)) {
            result->code = IsProcessRunning(kGameLauncherExe) ? "E_LAUNCHER_RUNNING" : "E_GAME_RUNNING";
            result->message = msg::FromUtf8(IsProcessRunning(kGameLauncherExe) ? msg::kLauncherRunning : msg::kGameRunning);
        } else {
            std::string code;
            FeedCheck feed;
            const FeedStatus feedStatus = FetchFeed(http, Narrow(LocalResourceRevision(gameDir)), feed, error);
            if (feedStatus == FeedStatus::Error) {
                result->code = feed.errorCode.empty() ? "E_NET" : feed.errorCode;
                message = L"Не удалось получить список обновлений: " + Widen(error);
            } else if (feedStatus == FeedStatus::NoPatch) {
                ok = true;
                result->noPatch = true;
                message = msg::NoPatch(Widen(feed.localRevision));
            } else {
                const Manifest& manifest = feed.patchManifest;
                InstallPlan plan;
                if (!BuildPlan(manifest, gameDir, plan, error)) {
                    result->code = plan.errorCode.empty() ? "E_TARGET" : plan.errorCode;
                    message = L"Не удалось найти файлы игры: " + Widen(error);
                } else if (!CheckGameVersion(plan, ReadGameVersion(gameDir), message)) {
                    result->code = "E_GAME_VERSION";
                } else if (!CheckGameRevision(plan, message, &code)) {
                    result->code = code;
                } else {
                    std::wstring detail;
                    const GateResult gate = CheckBefore(plan, stateDir_, detail, &code);
                    if (gate != GateResult::Ok) {
                        result->code = "E_VERSION_BEFORE";
                        message = GateMessage(plan, detail);
                    } else if (GameProcessesRunning(IsProcessRunning)) {
                        result->code = IsProcessRunning(kGameLauncherExe) ? "E_LAUNCHER_RUNNING" : "E_GAME_RUNNING";
                        message = msg::FromUtf8(IsProcessRunning(kGameLauncherExe) ? msg::kLauncherRunning : msg::kGameRunning);
                    } else {
                        phase_ = static_cast<int>(InstallPhase::Downloading);
                        const std::wstring sha12 = Widen(plan.zipSha256.substr(0, 12));
                        const std::wstring zip = downloadsDir_ + L"\\patch-" + Widen(plan.resourceRevision) + L"-" + sha12 + L".zip";
                        bool zipReady = false;
                        std::string existingHash;
                        if (FileExists(zip) && FileSize(zip) == plan.zipSize && Sha256File(zip, existingHash) && existingHash == plan.zipSha256)
                            zipReady = true;
                        if (!zipReady) {
                            const std::wstring part = zip + L".part";
                            DownloadResult download = DownloadToPart(
                                http, manifest.patch.url, part, plan.zipSize, plan.zipSha256,
                                [this](const char* progressPhase, std::uint64_t done, std::uint64_t total) {
                                    phase_ = std::strcmp(progressPhase, "verifying") == 0
                                                 ? static_cast<int>(InstallPhase::Verifying)
                                                 : static_cast<int>(InstallPhase::Downloading);
                                    done_ = done;
                                    total_ = total;
                                    return !cancel_.load();
                                },
                                cancel_);
                            if (!download.ok) {
                                result->code = download.cancelled ? "" : "E_DOWNLOAD";
                                message = download.cancelled ? L"Загрузка отменена." : L"Не удалось скачать перевод: " + Widen(download.error);
                            } else {
                                RemoveFile(zip);
                                if (!MoveFileExW(part.c_str(), zip.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                                    result->code = "E_DOWNLOAD";
                                    message = L"Не удалось сохранить архив перевода.";
                                } else {
                                    zipReady = true;
                                }
                            }
                        }
                        if (zipReady) {
                            const std::wstring staging = stagingDir_ + L"\\" + Widen(plan.resourceRevision) + L"-" +
                                                         Widen(plan.patchVersion) + L"-" + sha12;
                            InstallOptions options;
                            options.gameRunning = [] { return GameProcessesRunning(IsProcessRunning); };
                            options.phase = [this](InstallPhase installPhase) { phase_ = static_cast<int>(installPhase); };
                            options.progress = [this](const char*, std::uint64_t done, std::uint64_t total) {
                                done_ = done;
                                total_ = total;
                                return !cancel_.load();
                            };
                            options.cancel = &cancel_;
                            InstallReport report;
                            ok = ApplyInstall(plan, zip, staging, stateDir_, backupsDir_, options, report, message);
                            result->code = report.code;
                        }
                    }
                }
            }
        }
        if (!ok && message.empty()) message = error.empty() ? L"Не удалось установить перевод." : Widen(error);
        result->ok = ok;
        result->message = message;
        result->refresh = true;
        if (window_ && window_->hwnd()) {
            if (!PostMessageW(window_->hwnd(), kMsgJobDone, 0, reinterpret_cast<LPARAM>(result))) delete result;
        } else {
            delete result;
        }
    });
    if (window_) window_->Invalidate();
}

void App::StartRestore() {
    if (busy_) return;
    if (GameProcessesRunning(IsProcessRunning)) {
        SetError(msg::FromUtf8(IsProcessRunning(kGameLauncherExe) ? msg::kLauncherRunning : msg::kGameRunning));
        if (window_) window_->Invalidate();
        return;
    }
    if (worker_.joinable()) worker_.join();
    busy_ = true;
    cancel_ = false;
    view_.job = ui::Job::Restoring;
    view_.status = msg::FromUtf8(msg::kRestoring);
    view_.progress = 0.0;
    ClearError();
    const std::wstring gameDir = config_.gamePath;
    worker_ = std::thread([this, gameDir] {
        auto* result = new JobResult();
        std::wstring message;
        std::string code;
        const bool ok = RestoreInstall(gameDir, stateDir_, backupsDir_, [] { return GameProcessesRunning(IsProcessRunning); }, message, &code);
        result->ok = ok;
        result->code = code;
        result->message = message;
        result->refresh = true;
        if (window_ && window_->hwnd()) {
            if (!PostMessageW(window_->hwnd(), kMsgJobDone, 0, reinterpret_cast<LPARAM>(result))) delete result;
        } else {
            delete result;
        }
    });
    if (window_) window_->Invalidate();
}

void App::RecoverUnfinished() {
    if (busy_) return;
    if (worker_.joinable()) worker_.join();
    busy_ = true;
    cancel_ = false;
    view_.job = ui::Job::Restoring;
    view_.status = msg::FromUtf8(msg::kRollingBack);
    ClearError();
    const std::wstring gameDir = config_.gamePath;
    worker_ = std::thread([this, gameDir] {
        auto* result = new JobResult();
        std::wstring message;
        std::string code;
        const bool ok = RecoverJournal(gameDir, backupsDir_, [] { return GameProcessesRunning(IsProcessRunning); }, message, &code);
        result->ok = ok;
        result->code = code;
        result->message = message;
        result->refresh = true;
        view_.unfinished = false;
        if (window_ && window_->hwnd()) {
            if (!PostMessageW(window_->hwnd(), kMsgJobDone, 0, reinterpret_cast<LPARAM>(result))) delete result;
        } else {
            delete result;
        }
    });
    if (window_) window_->Invalidate();
}

void App::StartSelfUpdate() {
    if (busy_) return;
    if (worker_.joinable()) worker_.join();
    busy_ = true;
    cancel_ = false;
    view_.job = ui::Job::Updating;
    view_.status = msg::FromUtf8(msg::kSelfChecking);
    view_.progress = 0.0;
    ClearError();
    worker_ = std::thread([this] {
        auto* result = new JobResult();
        std::wstring message;
        std::string error;
        bool ok = false;
        WinHttpClient http;
        std::string body;
        Manifest manifest;
        std::string code;
        if (!FetchText(http, kFeedUrl, static_cast<std::size_t>(kManifestLimit), body, error)) {
            result->code = "E_NET";
            message = L"Не удалось проверить обновление BLUEWARD: " + Widen(error);
        } else if (!ParseManifest(body, manifest, error, &code)) {
            result->code = code;
            message = L"Список обновлений отклонён: " + Widen(error);
        } else if (!ShouldUpdate(kBluewardVersion, manifest.launcher)) {
            ok = true;
            message = L"Установлена последняя версия BLUEWARD.";
        } else {
            UpdateInfo info;
            info.version = manifest.launcher.version;
            info.url = manifest.launcher.url;
            info.sha256 = manifest.launcher.sha256;
            info.size = manifest.launcher.size;
            std::wstring newExe;
            const bool downloaded = DownloadUpdate(
                info, UpdateDir(), http,
                [this](const char* progressPhase, std::uint64_t done, std::uint64_t total) {
                    phase_ = std::strcmp(progressPhase, "verifying") == 0 ? static_cast<int>(InstallPhase::Verifying)
                                                                          : static_cast<int>(InstallPhase::Downloading);
                    done_ = done;
                    total_ = total;
                    return !cancel_.load();
                },
                cancel_, newExe, error);
            if (!downloaded) {
                result->code = error == "cancelled" ? "" : "E_SELFUPDATE_DOWNLOAD";
                message = error == "cancelled" ? L"Обновление отменено." : L"Не удалось скачать BLUEWARD: " + Widen(error);
            } else {
                std::wstring level;
                std::string levelError;
                const bool elevated = IsProcessElevated();
                if (!ReadExecutionLevel(newExe, level, levelError)) {
                    result->code = "E_SELFUPDATE_MANIFEST";
                    message = L"Не удалось прочитать манифест новой версии: " + Widen(levelError);
                } else if (elevated && level == L"asInvoker") {
                    result->code = "E_SELFUPDATE_ELEVATION";
                    message = L"Новая версия BLUEWARD не требует прав администратора, а текущая запущена с ними. Обновление отменено.";
                } else if (!elevated && (level == L"requireAdministrator" || level == L"highestAvailable")) {
                    result->code = "E_SELFUPDATE_ADMIN_REQUIRED";
                    message = L"Новая версия BLUEWARD требует прав администратора. Запустите обновление от имени администратора.";
                } else {
                    HandoffCallbacks callbacks;
                    callbacks.releaseMutex = releaseMutex_;
                    callbacks.reacquireMutex = reacquireMutex_;
                    if (!ApplyUpdateFiles(exePath_, newExe, GetCommandLineW(), callbacks, error)) {
                        result->code = "E_SELFUPDATE_START";
                        message = L"Не удалось обновить BLUEWARD: " + Widen(error);
                    } else {
                        ok = true;
                        result->quit = true;
                        message = L"BLUEWARD обновлён до " + Widen(info.version) + L".";
                    }
                }
            }
        }
        result->ok = ok;
        result->message = message;
        result->refresh = true;
        if (window_ && window_->hwnd()) {
            if (!PostMessageW(window_->hwnd(), kMsgJobDone, 0, reinterpret_cast<LPARAM>(result))) delete result;
        } else {
            delete result;
        }
    });
    if (window_) window_->Invalidate();
}

void App::CheckFeed(bool announce) {
    if (checkBusy_.exchange(true)) return;
    if (checkThread_.joinable()) checkThread_.join();
    {
        std::lock_guard lock(checkLock_);
        announceCheck_ = announce;
    }
    const std::wstring local = LocalResourceRevision(config_.gamePath);
    view_.feedChecking = true;
    checkThread_ = std::thread([this, local] {
        WinHttpClient http;
        std::string error;
        FeedCheck feed;
        const FeedStatus status = FetchFeed(http, Narrow(local), feed, error);
        {
            std::lock_guard lock(checkLock_);
            checkOk_ = status != FeedStatus::Error;
            checkFeed_ = std::move(feed);
            checkError_ = checkOk_ ? std::wstring() : (error.empty() ? L"Не удалось проверить обновления." : Widen(error));
        }
        checkBusy_ = false;
        if (window_ && window_->hwnd()) PostMessageW(window_->hwnd(), kMsgCheckDone, 0, 0);
    });
}

void App::CancelJob() {
    if (!busy_) return;
    cancel_ = true;
    view_.status = L"Отмена…";
    if (window_) window_->Invalidate();
}

void App::SaveSettings() {
    std::string error;
    if (!SaveConfig(DefaultConfigPath(), config_, error)) LogError(L"не удалось сохранить настройки: " + Widen(error));
}

void App::SetGamePath(const std::wstring& path) {
    config_.gamePath = path;
    view_.feedChecked = false;
    view_.feedChecking = false;
    view_.patchAvailable = false;
    view_.noPatch = false;
    view_.requiredRevision.clear();
    view_.latestPatch.clear();
    view_.updateAvailable = false;
    SaveSettings();
    RefreshState();
    LogInfo(L"папка игры: " + path);
    if (window_) window_->Invalidate();
}

void App::PickGameFolder() {
    if (!window_ || !window_->hwnd()) return;
    ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
        SetError(L"Не удалось открыть выбор папки.");
        return;
    }
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    dialog->SetTitle(L"Папка с AzurPromiliaJP.exe");
    if (dialog->Show(window_->hwnd()) != S_OK) return;
    ComPtr<IShellItem> item;
    if (FAILED(dialog->GetResult(&item))) return;
    PWSTR raw = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &raw)) || !raw) return;
    const std::wstring chosen(raw);
    CoTaskMemFree(raw);
    const std::wstring normalized = NormalizeGameDir(chosen);
    if (normalized.empty()) {
        SetError(msg::FromUtf8(msg::kGameInvalid));
        if (window_) window_->Invalidate();
        return;
    }
    SetGamePath(normalized);
}

void App::OpenGameFolder() {
    if (!IsGameDir(config_.gamePath)) return;
    if (!OpenFolderSelect(config_.gamePath)) ShellExecuteW(nullptr, nullptr, config_.gamePath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void App::OpenLog() {
    if (FileExists(logPath_)) OpenFolderSelect(logPath_);
    else OpenFolderSelect(logsDir_);
}

void App::OpenUrl(const std::wstring& url) {
    if (!TelegramEnabled(Narrow(url))) {
        LogWarn(L"ссылка Telegram отклонена: " + url);
        return;
    }
    ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void App::SetPage(ui::Page page) {
    view_.page = page;
    if (window_) window_->Invalidate();
}

void App::ToggleVideo() {
    config_.video = !config_.video;
    view_.video = config_.video;
    SaveSettings();
    if (window_) {
        window_->ApplyVideoMode();
        window_->Invalidate();
    }
}

void App::ToggleCheckUpdates() {
    config_.checkUpdates = !config_.checkUpdates;
    view_.checkUpdates = config_.checkUpdates;
    SaveSettings();
    if (window_) window_->Invalidate();
}

void App::SaveWindowPlacement(HWND hwnd) {
    if (!hwnd) return;
    WINDOWPLACEMENT placement{};
    placement.length = sizeof placement;
    if (!GetWindowPlacement(hwnd, &placement)) return;
    const RECT normal = placement.rcNormalPosition;
    config_.window.x = normal.left;
    config_.window.y = normal.top;
    config_.window.w = normal.right - normal.left;
    config_.window.h = normal.bottom - normal.top;
    config_.window.maximized = placement.showCmd == SW_SHOWMAXIMIZED;
    config_.window.valid = config_.window.w >= 760 && config_.window.h >= 520;
    SaveSettings();
}

}  // namespace blueward
