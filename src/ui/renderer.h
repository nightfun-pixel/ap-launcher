#pragma once
// Direct2D renderer for one frameless window: D3D11 flip swap chain, Direct2D device context,
// DirectWrite formats, the Media Foundation video background and the page. No controls, no
// retained scene; every frame is drawn from the UiModel and the Layout.

#include <d2d1_1.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <string>
#include <vector>

#include "media/video.h"
#include "ui/pages.h"

namespace blueward::ui {

enum class VideoMode { Off, Paused, Playing };

class Renderer {
public:
    Renderer() = default;
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    ~Renderer() { Shutdown(); }

    bool Init(HWND hwnd, bool videoWanted);
    void Shutdown();
    bool Reset();
    void Resize(unsigned widthPx, unsigned heightPx);

    // Draws one frame. Returns false when the window is occluded and the caller should stop its timer.
    bool Render(const UiModel& model, float width, float height, float dpi, const std::wstring& hoverId,
                const std::wstring& focusId);
    bool SavePng(const std::wstring& path);
    void RequestCapture(const std::wstring& path) { capturePath_ = path; }
    int CapturesDone() const { return capturesDone_; }
    int CapturesFailed() const { return capturesFailed_; }

    void SetVideoMode(VideoMode mode);
    VideoMode Mode() const { return videoMode_; }
    bool VideoRunning() const { return video_.Running(); }
    const media::VideoStats& VideoStats() const { return videoStats_; }
    bool VideoSupported() const { return videoSupported_; }
    bool HasEmbeddedVideo() const;
    void SetVideoEnabled(bool enabled) { videoWanted_ = enabled; }

private:
    bool CreateTarget();
    bool StartVideoNow();
    void CreateFormats(float dpi);
    void DrawBackground(float width, float height, float dpi);
    void DrawHeader(const UiModel& model, const Layout& layout, float width, float dpi, const std::wstring& hoverId,
                    const std::wstring& focusId);
    void DrawCard(const UiModel& model, const Layout& layout, float dpi, const std::wstring& hoverId, const std::wstring& focusId);
    void DrawText(const std::wstring& text, IDWriteTextFormat* format, const Rect& rect, D2D1_COLOR_F color,
                  DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING, bool clip = true, bool centerVertically = false);
    void Fill(const Rect& rect, D2D1_COLOR_F color);
    void Stroke(const Rect& rect, D2D1_COLOR_F color, float width = 1.0f);
    void DrawCover(ID2D1Bitmap1* bitmap, float width, float height);

    HWND hwnd_ = nullptr;
    float dpi_ = 96.0f;
    Microsoft::WRL::ComPtr<ID3D11Device> d3d_;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> swap_;
    Microsoft::WRL::ComPtr<ID2D1Factory1> d2d_;
    Microsoft::WRL::ComPtr<ID2D1Device> d2dDevice_;
    Microsoft::WRL::ComPtr<ID2D1DeviceContext> ctx_;
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> target_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush_;
    Microsoft::WRL::ComPtr<IDWriteFactory> dwrite_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> brand_, subtitle_, h1_, body_, small_, tiny_, button_, buttonSmall_, mono_, badge_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> title_, link_, wrap_;

    media::VideoBackground video_;
    media::VideoStats videoStats_{};
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> videoBitmap_;
    unsigned videoGeneration_ = 0;
    bool videoSupported_ = false;
    bool videoWanted_ = true;
    VideoMode videoMode_ = VideoMode::Off;
    std::wstring capturePath_;
    int capturesDone_ = 0, capturesFailed_ = 0;
};

}  // namespace blueward::ui
