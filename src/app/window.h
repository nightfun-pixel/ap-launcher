#pragma once
// Window class, frameless resize and drag, DPI, the frame timer and the video pause rules.

#include <windows.h>

#include <string>

#include "app/messages.h"
#include "ui/pages.h"
#include "ui/renderer.h"

namespace blueward {

class App;

class LauncherWindow {
public:
    LauncherWindow() = default;
    LauncherWindow(const LauncherWindow&) = delete;
    LauncherWindow& operator=(const LauncherWindow&) = delete;
    ~LauncherWindow();

    bool Create(HINSTANCE instance, App& app, int width, int height);
    int Run();
    HWND hwnd() const { return hwnd_; }
    void Invalidate();
    void StartTimer();
    void StopTimer();
    void ApplyVideoMode();
    void Frame();
    void SetShoot(const std::wstring& path, int seconds);

    static constexpr const wchar_t* kClassName = L"BluewardLauncherWindow";

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT Handle(UINT msg, WPARAM wp, LPARAM lp);
    LRESULT HitTest(LPARAM lp) const;
    float Scale() const;
    void UpdateSize();
    void OnJobDone(LPARAM lp);
    std::wstring focusId() const;
    void CopyToClipboard(const std::wstring& text);
    void Activate(const std::wstring& id);

    HWND hwnd_ = nullptr;
    App* app_ = nullptr;
    ui::Renderer renderer_;
    ui::Layout layout_;
    ui::Page page_ = ui::Page::Main;
    std::wstring hover_;
    float width_ = 1180.0f;
    float height_ = 720.0f;
    bool timerOn_ = false;
    bool minimized_ = false;
    bool occluded_ = false;
    bool active_ = true;
    bool trackingMouse_ = false;
    bool inFrame_ = false;
    int focusIndex_ = -1;
    std::wstring shootPath_;
    unsigned long long shootAt_ = 0;
};

}  // namespace blueward
