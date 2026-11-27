#include "app/window.h"

#include <dwmapi.h>
#include <windowsx.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <vector>

#include "app/app.h"
#include "core/gamepath.h"
#include "core/log.h"
#include "core/text.h"
#include "resource.h"
#include "ui/theme.h"

namespace blueward {

LauncherWindow::~LauncherWindow() = default;

bool LauncherWindow::Create(HINSTANCE instance, App& app, int width, int height) {
    app_ = &app;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof wc;
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_BLUEWARD));
    wc.hIconSm = wc.hIcon;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

    const float scale = static_cast<float>(GetDpiForSystem()) / 96.0f;
    const int pixelWidth = static_cast<int>(static_cast<float>(width) * scale);
    const int pixelHeight = static_cast<int>(static_cast<float>(height) * scale);
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int x = work.left + ((work.right - work.left) - pixelWidth) / 2;
    const int y = work.top + ((work.bottom - work.top) - pixelHeight) / 2;
    hwnd_ = CreateWindowExW(0, kClassName, L"BLUEWARD", WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX, x, y, pixelWidth,
                            pixelHeight, nullptr, nullptr, instance, this);
    if (!hwnd_) return false;
    width_ = static_cast<float>(width);
    height_ = static_cast<float>(height);

    const Config& config = app.Settings();
    if (config.window.valid) {
        RECT workArea{};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
        const int windowWidth = std::max(760, std::min(config.window.w, static_cast<int>(workArea.right - workArea.left)));
        const int windowHeight = std::max(520, std::min(config.window.h, static_cast<int>(workArea.bottom - workArea.top)));
        int windowX = std::max<int>(workArea.left, std::min<int>(config.window.x, static_cast<int>(workArea.right) - windowWidth));
        int windowY = std::max<int>(workArea.top, std::min<int>(config.window.y, static_cast<int>(workArea.bottom) - windowHeight));
        SetWindowPos(hwnd_, nullptr, windowX, windowY, windowWidth, windowHeight, SWP_NOZORDER | SWP_NOACTIVATE);
    }

    const MARGINS margins{1, 1, 1, 1};
    DwmExtendFrameIntoClientArea(hwnd_, &margins);
    const DWORD corner = 2;  // DWMWCP_ROUND
    DwmSetWindowAttribute(hwnd_, 33, &corner, sizeof corner);

    if (!renderer_.Init(hwnd_, app.Settings().video)) {
        LogError(L"не удалось создать устройство Direct3D");
        return false;
    }
    ShowWindow(hwnd_, SW_SHOW);
    if (config.window.valid && config.window.maximized) ShowWindow(hwnd_, SW_MAXIMIZE);
    UpdateWindow(hwnd_);
    ApplyVideoMode();
    StartTimer();
    return true;
}

int LauncherWindow::Run() {
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

void LauncherWindow::Invalidate() {
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void LauncherWindow::StartTimer() {
    if (!hwnd_ || timerOn_) return;
    SetTimer(hwnd_, 1, 16, nullptr);
    timerOn_ = true;
}

void LauncherWindow::StopTimer() {
    if (!hwnd_ || !timerOn_) return;
    KillTimer(hwnd_, 1);
    timerOn_ = false;
}

void LauncherWindow::ApplyVideoMode() {
    ui::VideoMode mode = ui::VideoMode::Playing;
    if (!app_->Settings().video) mode = ui::VideoMode::Off;
    else if (minimized_ || occluded_ || !active_) mode = ui::VideoMode::Paused;
    renderer_.SetVideoMode(mode);
}

float LauncherWindow::Scale() const {
    if (!hwnd_) return 1.0f;
    const UINT dpi = GetDpiForWindow(hwnd_);
    return dpi == 0 ? 1.0f : static_cast<float>(dpi) / 96.0f;
}

void LauncherWindow::UpdateSize() {
    if (!hwnd_) return;
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const float scale = Scale();
    width_ = static_cast<float>(rc.right - rc.left) / scale;
    height_ = static_cast<float>(rc.bottom - rc.top) / scale;
    if (width_ < 1.0f) width_ = 1.0f;
    if (height_ < 1.0f) height_ = 1.0f;
}

void LauncherWindow::Frame() {
    if (!hwnd_ || inFrame_) return;
    inFrame_ = true;
    app_->PollProgress();
    const ui::AppView& view = app_->View();
    page_ = view.page;
    const ui::UiModel model = page_ == ui::Page::Settings ? ui::BuildSettings(view) : ui::BuildMain(view);
    layout_ = ui::ComputeLayout(model, width_, height_);
    const float dpi = Scale() * 96.0f;
    const bool visible = renderer_.Render(model, width_, height_, dpi, hover_, focusId());
    if (!visible) {
        occluded_ = true;
        renderer_.SetVideoMode(ui::VideoMode::Paused);
        StopTimer();
    }
    if (!shootPath_.empty() && GetTickCount64() >= shootAt_) {
        renderer_.RequestCapture(shootPath_);
        renderer_.Render(model, width_, height_, dpi, hover_, focusId());
        const media::VideoStats stats = renderer_.VideoStats();
        LogInfo(L"снимок: " + shootPath_ + L", видео: декодировано " + std::to_wstring(stats.decoded) + L", показано " +
                std::to_wstring(stats.shown) + L", сбой: " + (stats.failed ? L"да" : L"нет"));
        shootPath_.clear();
        DestroyWindow(hwnd_);
    }
    inFrame_ = false;
}

std::wstring LauncherWindow::focusId() const {
    if (focusIndex_ < 0) return {};
    std::vector<std::wstring> ids;
    for (const ui::PlacedButton& button : layout_.headerButtons) ids.push_back(button.id);
    for (const ui::PlacedButton& button : layout_.buttons)
        if (button.enabled) ids.push_back(button.id);
    if (ids.empty()) return {};
    return ids[static_cast<std::size_t>(focusIndex_) % ids.size()];
}

void LauncherWindow::CopyToClipboard(const std::wstring& text) {
    if (!OpenClipboard(hwnd_)) return;
    EmptyClipboard();
    const std::size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory) {
        void* target = GlobalLock(memory);
        if (target) {
            std::memcpy(target, text.c_str(), bytes);
            GlobalUnlock(memory);
            SetClipboardData(CF_UNICODETEXT, memory);
        }
    }
    CloseClipboard();
}

void LauncherWindow::Activate(const std::wstring& id) {
    if (id.empty()) return;
    const ui::AppView& view = app_->View();
    if (id == L"close") DestroyWindow(hwnd_);
    else if (id == L"minimize") ShowWindow(hwnd_, SW_MINIMIZE);
    else if (id == L"settings") app_->SetPage(view.page == ui::Page::Settings ? ui::Page::Main : ui::Page::Settings);
    else if (id == L"telegram") app_->OpenUrl(Widen(app_->Settings().telegramUrl));
    else if (id == L"install") app_->StartInstall();
    else if (id == L"restore") app_->StartRestore();
    else if (id == L"update") app_->StartSelfUpdate();
    else if (id == L"cancel") app_->CancelJob();
    else if (id == L"pick") app_->PickGameFolder();
    else if (id == L"opengame") app_->OpenGameFolder();
    else if (id == L"toggle") app_->ToggleVideo();
    else if (id == L"toggles") app_->ToggleCheckUpdates();
    else if (id == L"openlog") app_->OpenLog();
    else if (id == L"back") app_->SetPage(ui::Page::Main);
    else if (id == L"recover") app_->RecoverUnfinished();
    else if (id == L"retry") {
        if (view.errorCode.find(L"RESTORE") != std::wstring::npos) app_->StartRestore();
        else app_->StartInstall();
    } else if (id == L"copy") {
        std::wstring text = view.error;
        if (!view.errorCode.empty()) text = L"[" + view.errorCode + L"] " + text;
        text += L"\n" + app_->LogPath();
        CopyToClipboard(text);
    }
    Invalidate();
}

void LauncherWindow::SetShoot(const std::wstring& path, int seconds) {
    shootPath_ = path;
    shootAt_ = GetTickCount64() + static_cast<unsigned long long>(std::max(1, seconds)) * 1000ull;
}

LRESULT LauncherWindow::HitTest(LPARAM lp) const {
    POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
    ScreenToClient(hwnd_, &pt);
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const int border = static_cast<int>(8.0f * Scale());
    if (!IsZoomed(hwnd_)) {
        const bool left = pt.x < border, right = pt.x >= rc.right - border, top = pt.y < border, bottom = pt.y >= rc.bottom - border;
        if (top && left) return HTTOPLEFT;
        if (top && right) return HTTOPRIGHT;
        if (bottom && left) return HTBOTTOMLEFT;
        if (bottom && right) return HTBOTTOMRIGHT;
        if (left) return HTLEFT;
        if (right) return HTRIGHT;
        if (top) return HTTOP;
        if (bottom) return HTBOTTOM;
    }
    const float x = static_cast<float>(pt.x) / Scale(), y = static_cast<float>(pt.y) / Scale();
    if (y < theme::kHeaderHeight) {
        for (const ui::PlacedButton& button : layout_.headerButtons)
            if (button.rect.Contains(x, y)) return HTCLIENT;
        return HTCAPTION;
    }
    return HTCLIENT;
}

void LauncherWindow::OnJobDone(LPARAM lp) {
    auto* result = reinterpret_cast<JobResult*>(lp);
    if (result) app_->TakeJobResult(result);
    Invalidate();
}

LRESULT LauncherWindow::Handle(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_NCCALCSIZE:
            if (wp) {
                if (IsZoomed(hwnd_)) {
                    auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lp);
                    const UINT dpi = GetDpiForWindow(hwnd_);
                    const int frame = GetSystemMetricsForDpi(SM_CXFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
                    params->rgrc[0].left += frame;
                    params->rgrc[0].right -= frame;
                    params->rgrc[0].top += frame;
                    params->rgrc[0].bottom -= frame;
                }
                return 0;
            }
            break;
        case WM_NCHITTEST:
            return HitTest(lp);
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lp);
            info->ptMinTrackSize.x = static_cast<LONG>(theme::kMinWidth * Scale());
            info->ptMinTrackSize.y = static_cast<LONG>(theme::kMinHeight * Scale());
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            BeginPaint(hwnd_, &ps);
            EndPaint(hwnd_, &ps);
            occluded_ = false;
            Frame();
            if (!occluded_) {
                StartTimer();
                ApplyVideoMode();
            }
            return 0;
        }
        case WM_SIZE:
            minimized_ = wp == SIZE_MINIMIZED;
            if (minimized_) {
                StopTimer();
                renderer_.SetVideoMode(ui::VideoMode::Paused);
            } else {
                UpdateSize();
                renderer_.Resize(static_cast<unsigned>(width_ * Scale()), static_cast<unsigned>(height_ * Scale()));
                Frame();
                StartTimer();
                ApplyVideoMode();
            }
            return 0;
        case WM_DPICHANGED: {
            const RECT* rect = reinterpret_cast<const RECT*>(lp);
            SetWindowPos(hwnd_, nullptr, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            renderer_.Reset();
            UpdateSize();
            Frame();
            return 0;
        }
        case WM_ACTIVATE:
            active_ = LOWORD(wp) != WA_INACTIVE;
            ApplyVideoMode();
            if (active_) {
                StartTimer();
                Invalidate();
            }
            return 0;
        case WM_TIMER:
            if (wp == 1) Frame();
            return 0;
        case WM_MOUSEMOVE: {
            if (!trackingMouse_) {
                TRACKMOUSEEVENT track{sizeof track, TME_LEAVE, hwnd_, 0};
                TrackMouseEvent(&track);
                trackingMouse_ = true;
            }
            const float x = static_cast<float>(GET_X_LPARAM(lp)) / Scale(), y = static_cast<float>(GET_Y_LPARAM(lp)) / Scale();
            std::wstring hover;
            for (const ui::PlacedButton& button : layout_.headerButtons)
                if (button.rect.Contains(x, y)) hover = button.id;
            for (const ui::PlacedButton& button : layout_.buttons)
                if (button.enabled && button.rect.Contains(x, y)) hover = button.id;
            if (hover != hover_) {
                hover_ = hover;
                SetCursor(LoadCursorW(nullptr, hover.empty() ? IDC_ARROW : IDC_HAND));
                Invalidate();
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            trackingMouse_ = false;
            if (!hover_.empty()) {
                hover_.clear();
                Invalidate();
            }
            return 0;
        case WM_SETCURSOR:
            if (LOWORD(lp) == HTCLIENT) {
                POINT pt{};
                GetCursorPos(&pt);
                ScreenToClient(hwnd_, &pt);
                const float x = static_cast<float>(pt.x) / Scale(), y = static_cast<float>(pt.y) / Scale();
                bool hand = false;
                for (const ui::PlacedButton& button : layout_.headerButtons)
                    if (button.rect.Contains(x, y)) hand = true;
                for (const ui::PlacedButton& button : layout_.buttons)
                    if (button.enabled && button.rect.Contains(x, y)) hand = true;
                SetCursor(LoadCursorW(nullptr, hand ? IDC_HAND : IDC_ARROW));
                return TRUE;
            }
            break;
        case WM_LBUTTONUP: {
            const float x = static_cast<float>(GET_X_LPARAM(lp)) / Scale(), y = static_cast<float>(GET_Y_LPARAM(lp)) / Scale();
            std::wstring id;
            for (const ui::PlacedButton& button : layout_.headerButtons)
                if (button.rect.Contains(x, y)) id = button.id;
            for (const ui::PlacedButton& button : layout_.buttons)
                if (button.enabled && button.rect.Contains(x, y)) id = button.id;
            if (id.empty()) return 0;
            focusIndex_ = -1;
            Activate(id);
            return 0;
        }
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE) {
                if (app_->View().page == ui::Page::Settings) app_->SetPage(ui::Page::Main);
                else DestroyWindow(hwnd_);
                return 0;
            }
            if (wp == VK_TAB) {
                std::vector<std::wstring> ids;
                for (const ui::PlacedButton& button : layout_.headerButtons) ids.push_back(button.id);
                for (const ui::PlacedButton& button : layout_.buttons)
                    if (button.enabled) ids.push_back(button.id);
                if (!ids.empty()) {
                    const int step = (GetKeyState(VK_SHIFT) & 0x8000) ? -1 : 1;
                    const int count = static_cast<int>(ids.size());
                    focusIndex_ = focusIndex_ < 0 ? (step > 0 ? 0 : count - 1) : (focusIndex_ + step + count) % count;
                    Invalidate();
                }
                return 0;
            }
            if (wp == VK_RETURN || wp == VK_SPACE) {
                const std::wstring id = focusId();
                if (!id.empty()) {
                    const bool launcherRunning = false;
                    (void)launcherRunning;
                    Activate(id);
                    return 0;
                }
            }
            return 0;
        case kMsgActivate:
            if (IsIconic(hwnd_)) ShowWindow(hwnd_, SW_RESTORE);
            else ShowWindow(hwnd_, SW_SHOW);
            SetForegroundWindow(hwnd_);
            Invalidate();
            return 0;
        case kMsgJobDone:
            OnJobDone(lp);
            return 0;
        case kMsgCheckDone:
            app_->TakeCheckResult();
            Invalidate();
            return 0;
        case WM_CLOSE:
            DestroyWindow(hwnd_);
            return 0;
        case WM_DESTROY:
            StopTimer();
            renderer_.SetVideoMode(ui::VideoMode::Off);
            app_->SaveWindowPlacement(hwnd_);
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

LRESULT CALLBACK LauncherWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lp);
        auto* self = static_cast<LauncherWindow*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        if (self) self->hwnd_ = hwnd;
    }
    auto* self = reinterpret_cast<LauncherWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!self) return DefWindowProcW(hwnd, msg, wp, lp);
    return self->Handle(msg, wp, lp);
}

}  // namespace blueward
