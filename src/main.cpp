// BLUEWARD entry point: DPI, COM, one instance, the launcher window and its message loop.

#include <windows.h>
#include <objbase.h>
#include <shellapi.h>

#include <string>

#include "app/app.h"
#include "app/messages.h"
#include "app/single_instance.h"
#include "app/window.h"
#include "core/config.h"
#include "core/log.h"
#include "ui/theme.h"

namespace {

std::wstring HitArgument(int argc, wchar_t** argv, const wchar_t* name) {
    for (int i = 1; i + 1 < argc; ++i)
        if (_wcsicmp(argv[i], name) == 0) return argv[i + 1];
    return {};
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    const wchar_t* kMutexName = L"Local\\BluewardLauncher";
    blueward::SingleInstance singleInstance;
    std::string mutexError;
    if (!singleInstance.Acquire(kMutexName, mutexError)) {
        if (mutexError == "ERROR_ALREADY_EXISTS")
            blueward::ActivateExistingWindow(blueward::LauncherWindow::kClassName, blueward::kMsgActivate);
        CoUninitialize();
        return 0;
    }

    blueward::App app;
    if (!app.Init(instance)) {
        MessageBoxW(nullptr,
                    L"Не удалось запустить BLUEWARD. Журнал: %LOCALAPPDATA%\\BLUEWARD\\logs\\launcher.log.",
                    L"BLUEWARD", MB_ICONERROR | MB_OK);
        CoUninitialize();
        return 1;
    }
    app.SetMutexHooks([&singleInstance, kMutexName] { singleInstance.Release(); },
                      [&singleInstance, kMutexName, &mutexError] { return singleInstance.Acquire(kMutexName, mutexError); });

    blueward::LauncherWindow window;
    if (!window.Create(instance, app, static_cast<int>(blueward::theme::kDefaultWidth),
                       static_cast<int>(blueward::theme::kDefaultHeight))) {
        MessageBoxW(nullptr, L"Не удалось создать окно BLUEWARD. Журнал: %LOCALAPPDATA%\\BLUEWARD\\logs\\launcher.log.",
                    L"BLUEWARD", MB_ICONERROR | MB_OK);
        app.Shutdown();
        CoUninitialize();
        return 1;
    }
    app.AttachWindow(&window);
    app.CheckFeed(false);

    int argc = 0;
    if (LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc)) {
        const std::wstring shoot = HitArgument(argc, argv, L"--shoot");
        if (!shoot.empty()) window.SetShoot(shoot, 3);
        LocalFree(argv);
    }

    const int code = window.Run();
    app.Shutdown();
    CoUninitialize();
    return code;
}
