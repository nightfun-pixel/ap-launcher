#include "check.h"

#include <string>

#include "app/single_instance.h"
#include "app/messages.h"
#include "core/text.h"

namespace {

volatile LONG g_activated = 0;

LRESULT CALLBACK TestProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == blueward::kMsgActivate) {
        InterlockedIncrement(&g_activated);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

std::wstring UniqueName() {
    return L"Local\\BluewardTest-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
}

}  // namespace

void RunSingleInstanceTests() {
    {
        const std::wstring name = UniqueName();
        blueward::SingleInstance first;
        std::string error;
        CHECK_MSG(first.Acquire(name, error), error);
        CHECK(first.Owned());
        {
            blueward::SingleInstance second;
            std::string secondError;
            CHECK(!second.Acquire(name, secondError));
            CHECK(secondError.find("ALREADY_EXISTS") != std::string::npos);
            CHECK(!second.Owned());
        }
        first.~SingleInstance();
        new (&first) blueward::SingleInstance();  // the destructor above released the name
        error.clear();
        CHECK_MSG(first.Acquire(name, error), error);
    }
    {
        CHECK(!blueward::ActivateExistingWindow(L"BluewardNoSuchWindowClass", blueward::kMsgActivate));
    }
    {
        WNDCLASSW wc{};
        wc.lpfnWndProc = TestProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"BluewardLauncherWindow";
        const ATOM atom = RegisterClassW(&wc);
        CHECK(atom != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS);
        HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"hidden", WS_OVERLAPPEDWINDOW, 0, 0, 200, 120, nullptr, nullptr,
                                    wc.hInstance, nullptr);
        CHECK(hwnd != nullptr);
        CHECK(blueward::ActivateExistingWindow(wc.lpszClassName, blueward::kMsgActivate));
        MSG msg{};
        while (PeekMessageW(&msg, hwnd, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        CHECK(InterlockedCompareExchange(&g_activated, 0, 0) == 1);
        DestroyWindow(hwnd);
    }
}
