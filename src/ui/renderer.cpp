#include "ui/renderer.h"

#include <d2d1_1helper.h>
#include <d3d11_4.h>
#include <dwmapi.h>
#include <wincodec.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "core/log.h"
#include "resource.h"
#include "ui/theme.h"

using Microsoft::WRL::ComPtr;

namespace blueward::ui {

namespace {

D2D1_COLOR_F WithAlpha(D2D1_COLOR_F color, float alpha) { return D2D1::ColorF(color.r, color.g, color.b, alpha); }

D2D1_ROUNDED_RECT Rounded(const Rect& rect, float radius) {
    return D2D1::RoundedRect(D2D1::RectF(rect.x, rect.y, rect.x + rect.w, rect.y + rect.h), radius, radius);
}

}  // namespace

bool Renderer::HasEmbeddedVideo() const { return media::HasEmbeddedVideo(); }

bool Renderer::Init(HWND hwnd, bool videoWanted) {
    hwnd_ = hwnd;
    videoWanted_ = videoWanted;
    dpi_ = static_cast<float>(GetDpiForWindow(hwnd));
    if (dpi_ <= 0) dpi_ = 96.0f;

    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
    videoSupported_ = true;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT,
                                   levels, ARRAYSIZE(levels), D3D11_SDK_VERSION, &d3d_, nullptr, nullptr);
    if (FAILED(hr)) {
        videoSupported_ = false;
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, ARRAYSIZE(levels),
                               D3D11_SDK_VERSION, &d3d_, nullptr, nullptr);
    }
    if (FAILED(hr))
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, ARRAYSIZE(levels),
                               D3D11_SDK_VERSION, &d3d_, nullptr, nullptr);
    if (FAILED(hr)) return false;
    {
        ComPtr<ID3D11Multithread> multithread;
        if (SUCCEEDED(d3d_.As(&multithread))) multithread->SetMultithreadProtected(TRUE);
        else videoSupported_ = false;
    }

    ComPtr<IDXGIDevice> dxgiDevice;
    ComPtr<IDXGIAdapter> adapter;
    ComPtr<IDXGIFactory2> factory;
    if (FAILED(d3d_.As(&dxgiDevice)) || FAILED(dxgiDevice->GetAdapter(&adapter)) || FAILED(adapter->GetParent(IID_PPV_ARGS(&factory))))
        return false;

    RECT rc{};
    GetClientRect(hwnd, &rc);
    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = static_cast<UINT>(std::max<LONG>(1, rc.right - rc.left));
    desc.Height = static_cast<UINT>(std::max<LONG>(1, rc.bottom - rc.top));
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.Scaling = DXGI_SCALING_NONE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    if (FAILED(factory->CreateSwapChainForHwnd(d3d_.Get(), hwnd, &desc, nullptr, nullptr, &swap_))) return false;
    factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

    D2D1_FACTORY_OPTIONS options{};
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &options, &d2d_))) return false;
    if (FAILED(d2d_->CreateDevice(dxgiDevice.Get(), &d2dDevice_))) return false;
    if (FAILED(d2dDevice_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &ctx_))) return false;
    if (FAILED(ctx_->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1), &brush_))) return false;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(dwrite_.GetAddressOf()))))
        return false;
    CreateFormats(dpi_);
    if (!CreateTarget()) return false;
    videoMode_ = VideoMode::Off;
    if (videoWanted_) SetVideoMode(VideoMode::Playing);
    return true;
}

void Renderer::CreateFormats(float dpi) {
    (void)dpi;
    auto make = [&](ComPtr<IDWriteTextFormat>& format, const wchar_t* family, DWRITE_FONT_WEIGHT weight, float size,
                    DWRITE_WORD_WRAPPING wrapping = DWRITE_WORD_WRAPPING_NO_WRAP) {
        if (FAILED(dwrite_->CreateTextFormat(family, nullptr, weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"ru-ru", &format)))
            return false;
        format->SetWordWrapping(wrapping);
        format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        return true;
    };
    make(brand_, L"Segoe UI Semibold", DWRITE_FONT_WEIGHT_SEMI_BOLD, 19.0f);
    make(subtitle_, L"Segoe UI", DWRITE_FONT_WEIGHT_NORMAL, 12.0f);
    make(h1_, L"Segoe UI Semibold", DWRITE_FONT_WEIGHT_SEMI_BOLD, 27.0f);
    make(body_, L"Segoe UI", DWRITE_FONT_WEIGHT_NORMAL, 14.0f);
    make(small_, L"Segoe UI", DWRITE_FONT_WEIGHT_NORMAL, 12.5f);
    make(tiny_, L"Segoe UI", DWRITE_FONT_WEIGHT_NORMAL, 11.0f);
    make(button_, L"Segoe UI Semibold", DWRITE_FONT_WEIGHT_SEMI_BOLD, 14.5f);
    make(buttonSmall_, L"Segoe UI Semibold", DWRITE_FONT_WEIGHT_SEMI_BOLD, 12.5f);
    make(mono_, L"Consolas", DWRITE_FONT_WEIGHT_NORMAL, 12.0f);
    make(badge_, L"Segoe UI Semibold", DWRITE_FONT_WEIGHT_SEMI_BOLD, 11.5f);
    make(title_, L"Segoe UI Semibold", DWRITE_FONT_WEIGHT_SEMI_BOLD, 17.0f);
    make(link_, L"Segoe UI Semibold", DWRITE_FONT_WEIGHT_SEMI_BOLD, 13.0f);
    make(wrap_, L"Segoe UI", DWRITE_FONT_WEIGHT_NORMAL, 12.5f, DWRITE_WORD_WRAPPING_WRAP);
}

bool Renderer::StartVideoNow() {
    if (!videoSupported_ || !target_) return false;
    const D2D1_SIZE_U size = target_->GetPixelSize();
    return video_.Start(d3d_.Get(), L"", size.width, size.height, IDR_BG_720);
}

void Renderer::SetVideoMode(VideoMode mode) {
    videoMode_ = mode;
    switch (mode) {
        case VideoMode::Off:
            if (video_.Running()) {
                video_.Update();
                video_.Stop();
                videoBitmap_.Reset();
            }
            break;
        case VideoMode::Paused:
            if (video_.Running() || StartVideoNow()) video_.SetPaused(true);
            break;
        case VideoMode::Playing:
            if (!videoWanted_) {
                if (video_.Running()) video_.Stop();
                videoBitmap_.Reset();
                videoMode_ = VideoMode::Off;
                return;
            }
            if (video_.Running()) video_.SetPaused(false);
            else StartVideoNow();
            break;
    }
}

bool Renderer::CreateTarget() {
    ComPtr<IDXGISurface> surface;
    if (FAILED(swap_->GetBuffer(0, IID_PPV_ARGS(&surface)))) return false;
    const D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        dpi_, dpi_);
    if (FAILED(ctx_->CreateBitmapFromDxgiSurface(surface.Get(), &props, &target_))) return false;
    ctx_->SetTarget(target_.Get());
    return true;
}

void Renderer::Shutdown() {
    video_.Stop();
    videoBitmap_.Reset();
    if (ctx_) ctx_->SetTarget(nullptr);
    target_.Reset();
    brush_.Reset();
    brand_.Reset();
    subtitle_.Reset();
    h1_.Reset();
    body_.Reset();
    small_.Reset();
    tiny_.Reset();
    button_.Reset();
    buttonSmall_.Reset();
    mono_.Reset();
    badge_.Reset();
    title_.Reset();
    link_.Reset();
    wrap_.Reset();
    dwrite_.Reset();
    ctx_.Reset();
    d2dDevice_.Reset();
    d2d_.Reset();
    swap_.Reset();
    d3d_.Reset();
    videoStats_ = {};
}

bool Renderer::Reset() {
    Shutdown();
    return Init(hwnd_, videoWanted_);
}

void Renderer::Resize(unsigned widthPx, unsigned heightPx) {
    if (!swap_ || widthPx == 0 || heightPx == 0) return;
    ctx_->SetTarget(nullptr);
    target_.Reset();
    videoBitmap_.Reset();
    if (FAILED(swap_->ResizeBuffers(0, widthPx, heightPx, DXGI_FORMAT_UNKNOWN, 0)) || !CreateTarget()) {
        Shutdown();
        Init(hwnd_, videoWanted_);
    }
}

void Renderer::Fill(const Rect& rect, D2D1_COLOR_F color) {
    if (!ctx_) return;
    brush_->SetColor(color);
    ctx_->FillRectangle(D2D1::RectF(rect.x, rect.y, rect.x + rect.w, rect.y + rect.h), brush_.Get());
}

void Renderer::Stroke(const Rect& rect, D2D1_COLOR_F color, float width) {
    if (!ctx_) return;
    brush_->SetColor(color);
    ctx_->DrawRectangle(D2D1::RectF(rect.x, rect.y, rect.x + rect.w, rect.y + rect.h), brush_.Get(), width);
}

void Renderer::DrawText(const std::wstring& text, IDWriteTextFormat* format, const Rect& rect, D2D1_COLOR_F color,
                        DWRITE_TEXT_ALIGNMENT align, bool clip, bool centerVertically) {
    if (!ctx_ || !format) return;
    format->SetTextAlignment(align);
    format->SetParagraphAlignment(centerVertically ? DWRITE_PARAGRAPH_ALIGNMENT_CENTER : DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    brush_->SetColor(color);
    ctx_->DrawTextW(text.c_str(), static_cast<UINT32>(text.size()), format, D2D1::RectF(rect.x, rect.y, rect.x + rect.w, rect.y + rect.h),
                    brush_.Get(), clip ? D2D1_DRAW_TEXT_OPTIONS_CLIP : D2D1_DRAW_TEXT_OPTIONS_NONE);
}

void Renderer::DrawCover(ID2D1Bitmap1* bitmap, float width, float height) {
    const D2D1_SIZE_F size = bitmap->GetSize();
    if (size.width <= 0 || size.height <= 0) return;
    const float scale = std::max(width / size.width, height / size.height);
    const float sourceWidth = width / scale, sourceHeight = height / scale;
    const D2D1_RECT_F source = D2D1::RectF((size.width - sourceWidth) / 2, (size.height - sourceHeight) / 2,
                                           (size.width + sourceWidth) / 2, (size.height + sourceHeight) / 2);
    ctx_->DrawBitmap(bitmap, D2D1::RectF(0, 0, width, height), 1.0f, D2D1_INTERPOLATION_MODE_LINEAR, &source);
}

void Renderer::DrawBackground(float width, float height, float dpi) {
    (void)dpi;
    ctx_->Clear(theme::kBg);
    if (videoBitmap_) {
        DrawCover(videoBitmap_.Get(), width, height);
        auto linear = [&](float x0, float y0, float x1, float y1, std::initializer_list<D2D1_GRADIENT_STOP> stops) {
            ComPtr<ID2D1GradientStopCollection> collection;
            ComPtr<ID2D1LinearGradientBrush> gradient;
            if (SUCCEEDED(ctx_->CreateGradientStopCollection(stops.begin(), static_cast<UINT32>(stops.size()), &collection)) &&
                SUCCEEDED(ctx_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(x0, y0), D2D1::Point2F(x1, y1)),
                                                          collection.Get(), &gradient)))
                ctx_->FillRectangle(D2D1::RectF(0, 0, width, height), gradient.Get());
        };
        // A left-to-right scrim keeps the wordmark readable without hiding the key visual.
        linear(0, 0, width * 0.78f, 0, {{0.0f, WithAlpha(theme::kBg, 0.76f)}, {0.42f, WithAlpha(theme::kBg, 0.52f)}, {1.0f, WithAlpha(theme::kBg, 0.08f)}});
        linear(0, height * 0.55f, 0, height, {{0.0f, WithAlpha(theme::kBg, 0.0f)}, {1.0f, WithAlpha(theme::kBg, 0.88f)}});
        linear(0, 0, 0, 110, {{0.0f, WithAlpha(theme::kBg, 0.62f)}, {1.0f, WithAlpha(theme::kBg, 0.0f)}});
    } else {
        Fill({0, 0, width, height}, theme::kBg);
        auto radial = [&](float cx, float cy, float radius, D2D1_COLOR_F inner, D2D1_COLOR_F outer) {
            const D2D1_GRADIENT_STOP stops[2] = {{0.0f, inner}, {1.0f, outer}};
            ComPtr<ID2D1GradientStopCollection> collection;
            ComPtr<ID2D1RadialGradientBrush> gradient;
            if (SUCCEEDED(ctx_->CreateGradientStopCollection(stops, 2, &collection))) {
                const D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES props{D2D1::Point2F(cx, cy), D2D1::Point2F(0, 0), radius, radius};
                if (SUCCEEDED(ctx_->CreateRadialGradientBrush(props, collection.Get(), &gradient)))
                    ctx_->FillRectangle(D2D1::RectF(0, 0, width, height), gradient.Get());
            }
        };
        radial(width * 0.68f, height * 0.32f, std::max(width, height) * 0.62f, WithAlpha(theme::kAccent, 0.24f),
               WithAlpha(theme::kAccent, 0.0f));
        radial(width * 0.12f, height * 0.82f, std::max(width, height) * 0.48f, WithAlpha(theme::kLink, 0.18f),
               WithAlpha(theme::kLink, 0.0f));
    }
}

void Renderer::DrawHeader(const UiModel& model, const Layout& layout, float width, float dpi, const std::wstring& hoverId,
                          const std::wstring& focusId) {
    (void)dpi;
    DrawText(L"BLUEWARD", brand_.Get(), {24, 6, 260, 26}, theme::kAccent);
    DrawText(L"AZUR PROMILIA · ПАТЧЕР ПЕРЕВОДА", tiny_.Get(), {25, 32, 360, 16}, WithAlpha(theme::kMuted, 0.9f));
    Fill({0, theme::kHeaderHeight - 1, width, 1}, WithAlpha(theme::kPanelRaised, 0.55f));

    for (const PlacedButton& button : layout.headerButtons) {
        const bool hover = button.id == hoverId;
        if (hover) {
            brush_->SetColor(WithAlpha(theme::kPanelRaised, 0.85f));
            ctx_->FillRoundedRectangle(Rounded(button.rect, 8.0f), brush_.Get());
        }
        if (!focusId.empty() && button.id == focusId) {
            brush_->SetColor(theme::kAccent);
            ctx_->DrawRoundedRectangle(Rounded(button.rect, 8.0f), brush_.Get(), 2.0f);
        }
        if (button.id == L"close") {
            DrawText(L"×", h1_.Get(), button.rect, hover ? theme::kText : theme::kMuted, DWRITE_TEXT_ALIGNMENT_CENTER, true, true);
        } else if (button.id == L"minimize") {
            DrawText(L"–", h1_.Get(), {button.rect.x, button.rect.y - 6, button.rect.w, button.rect.h + 6}, hover ? theme::kText : theme::kMuted,
                     DWRITE_TEXT_ALIGNMENT_CENTER, true, true);
        } else if (button.id == L"settings") {
            DrawText(model.page == Page::Settings ? L"Готово" : L"Настройки", buttonSmall_.Get(), button.rect,
                     hover ? theme::kText : WithAlpha(theme::kText, 0.85f), DWRITE_TEXT_ALIGNMENT_CENTER, true, true);
        } else if (button.id == L"telegram") {
            DrawText(L"Telegram", link_.Get(), button.rect, hover ? theme::kText : theme::kLink, DWRITE_TEXT_ALIGNMENT_CENTER, true, true);
        }
    }
}

void Renderer::DrawCard(const UiModel& model, const Layout& layout, float dpi, const std::wstring& hoverId,
                        const std::wstring& focusId) {
    (void)dpi;
    brush_->SetColor(WithAlpha(theme::kPanel, 0.94f));
    ctx_->FillRoundedRectangle(Rounded(layout.card, 16.0f), brush_.Get());
    brush_->SetColor(WithAlpha(theme::kPanelRaised, 0.85f));
    ctx_->DrawRoundedRectangle(Rounded(layout.card, 16.0f), brush_.Get(), 1.0f);

    DrawText(model.title, h1_.Get(), {layout.card.x + 28, layout.card.y + 12, layout.card.w - 56, 38}, theme::kText);
    DrawText(model.subtitle, small_.Get(), {layout.card.x + 28, layout.card.y + 48, layout.card.w - 56, 20}, theme::kMuted);

    for (std::size_t i = 0; i < model.rows.size() && i < layout.rows.size(); ++i) {
        const UiRow& row = model.rows[i];
        const Rect& rect = layout.rows[i].rect;
        DrawText(row.label, tiny_.Get(), {rect.x, rect.y, rect.w, 15}, WithAlpha(theme::kMuted, 0.9f));
        D2D1_COLOR_F valueColor = row.emphasis ? theme::kGold : theme::kText;
        if (row.kind == L"path") {
            DrawText(row.value, mono_.Get(), {rect.x, rect.y + 15, rect.w, 22}, valueColor);
        } else {
            DrawText(row.value, small_.Get(), {rect.x, rect.y + 15, rect.w, 22}, valueColor);
        }
        Fill({rect.x, rect.y + rect.h - 1, rect.w, 1}, WithAlpha(theme::kPanelRaised, 0.45f));
    }

    for (const PlacedButton& button : layout.buttons) {
        const UiButton* source = nullptr;
        for (const UiButton& candidate : model.buttons)
            if (candidate.id == button.id) source = &candidate;
        if (!source) continue;
        const bool hover = button.enabled && button.id == hoverId;
        const float alpha = button.enabled ? 1.0f : 0.42f;
        if (button.primary) {
            brush_->SetColor(WithAlpha(hover ? theme::kButtonHover : theme::kButton, alpha));
            ctx_->FillRoundedRectangle(Rounded(button.rect, 11.0f), brush_.Get());
        } else {
            brush_->SetColor(WithAlpha(hover ? theme::kPanelRaised : theme::kPanel, 0.92f * alpha));
            ctx_->FillRoundedRectangle(Rounded(button.rect, 11.0f), brush_.Get());
            brush_->SetColor(WithAlpha(theme::kPanelRaised, 0.95f * alpha));
            ctx_->DrawRoundedRectangle(Rounded(button.rect, 11.0f), brush_.Get(), 1.0f);
        }
        DrawText(source->label, button.primary ? button_.Get() : buttonSmall_.Get(), button.rect,
                 WithAlpha(button.primary ? theme::kText : (hover ? theme::kText : theme::kMuted), alpha), DWRITE_TEXT_ALIGNMENT_CENTER, true, true);
        if (!focusId.empty() && button.id == focusId) {
            brush_->SetColor(theme::kAccent);
            ctx_->DrawRoundedRectangle(Rounded(button.rect, 11.0f), brush_.Get(), 2.0f);
        }
    }

    float buttonTop = layout.card.y + layout.card.h - 24.0f;
    for (const PlacedButton& button : layout.buttons) buttonTop = std::min(buttonTop, button.rect.y);
    if (model.showProgress) {
        brush_->SetColor(WithAlpha(theme::kPanelRaised, 0.95f));
        ctx_->FillRoundedRectangle(Rounded(layout.progress, 3.0f), brush_.Get());
        if (model.progress > 0.0) {
            Rect fill = layout.progress;
            fill.w = static_cast<float>(fill.w) * static_cast<float>(model.progress);
            if (fill.w < 6.0f) fill.w = 6.0f;
            brush_->SetColor(theme::kAccent);
            ctx_->FillRoundedRectangle(Rounded(fill, 3.0f), brush_.Get());
        }
        DrawText(model.status, tiny_.Get(), {layout.progress.x, layout.progress.y - 20, layout.progress.w - 80, 16},
                 WithAlpha(theme::kMuted, 0.95f));
        DrawText(model.percent, tiny_.Get(), {layout.progress.x + layout.progress.w - 76, layout.progress.y - 20, 76, 16}, theme::kAccent,
                 DWRITE_TEXT_ALIGNMENT_TRAILING);    }

    const float messageTop = layout.contentBottom + 8.0f;
    const float messageHeight = std::max(0.0f, buttonTop - messageTop - 12.0f);
    if (!model.error.empty()) {
        std::wstring text = model.error;
        if (!model.errorCode.empty()) text = L"[" + model.errorCode + L"] " + text;
        DrawText(text, wrap_.Get(), {layout.card.x + 28, messageTop, layout.card.w - 56, messageHeight}, theme::kDanger);
    } else if (!model.status.empty()) {
        DrawText(model.status, wrap_.Get(), {layout.card.x + 28, messageTop, layout.card.w - 56, messageHeight},
                 WithAlpha(theme::kMuted, 0.95f));
    }
    if (!model.footer.empty())
        DrawText(model.footer, tiny_.Get(), {layout.card.x + 28, layout.card.y + layout.card.h - 18, layout.card.w - 56, 14},
                 WithAlpha(theme::kMuted, 0.75f), DWRITE_TEXT_ALIGNMENT_CENTER);
}

bool Renderer::Render(const UiModel& model, float width, float height, float dpi, const std::wstring& hoverId,
                      const std::wstring& focusId) {
    if (!ctx_ && !Init(hwnd_, videoWanted_)) return true;
    dpi_ = dpi;
    bool backdrop = false;
    if (video_.Running()) {
        video_.Update();
        if (ID3D11Texture2D* frame = video_.Frame()) {
            if (!videoBitmap_ || videoGeneration_ != video_.FrameGeneration()) {
                videoBitmap_.Reset();
                ComPtr<IDXGISurface> surface;
                const D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
                    D2D1_BITMAP_OPTIONS_NONE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), 96.0f, 96.0f);
                if (SUCCEEDED(frame->QueryInterface(IID_PPV_ARGS(&surface))))
                    ctx_->CreateBitmapFromDxgiSurface(surface.Get(), &props, &videoBitmap_);
                videoGeneration_ = video_.FrameGeneration();
            }
            backdrop = videoBitmap_ != nullptr;
        }
        videoStats_ = video_.Stats();
    }

    const Layout layout = ComputeLayout(model, width, height);
    ctx_->BeginDraw();
    ctx_->SetTransform(D2D1::Matrix3x2F::Identity());
    DrawBackground(width, height, dpi);
    DrawHeader(model, layout, width, dpi, hoverId, focusId);
    DrawCard(model, layout, dpi, hoverId, focusId);
    (void)backdrop;
    const HRESULT drawn = ctx_->EndDraw();
    if (drawn == D2DERR_RECREATE_TARGET) {
        Shutdown();
        return true;
    }
    if (!capturePath_.empty()) {
        if (SavePng(capturePath_)) ++capturesDone_;
        else ++capturesFailed_;
        LogInfo(std::wstring(L"снимок окна: ") + (capturesFailed_ == 0 ? L"сохранён" : L"не удался"));
        capturePath_.clear();
    }
    const HRESULT shown = swap_->Present(1, 0);
    if (shown == DXGI_STATUS_OCCLUDED) return false;
    if (shown == DXGI_ERROR_DEVICE_REMOVED || shown == DXGI_ERROR_DEVICE_RESET) Shutdown();
    return true;
}

bool Renderer::SavePng(const std::wstring& path) {
    ComPtr<ID3D11Texture2D> back;
    if (!swap_ || FAILED(swap_->GetBuffer(0, IID_PPV_ARGS(&back)))) return false;
    D3D11_TEXTURE2D_DESC desc{};
    back->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> staging;
    ComPtr<ID3D11DeviceContext> context;
    if (FAILED(d3d_->CreateTexture2D(&desc, nullptr, &staging))) return false;
    d3d_->GetImmediateContext(&context);
    context->CopyResource(staging.Get(), back.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return false;

    bool ok = false;
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> options;
    std::vector<BYTE> pixels(static_cast<std::size_t>(desc.Width) * desc.Height * 4);
    for (UINT y = 0; y < desc.Height; ++y) {
        const BYTE* from = static_cast<const BYTE*>(mapped.pData) + static_cast<std::size_t>(y) * mapped.RowPitch;
        BYTE* to = pixels.data() + static_cast<std::size_t>(y) * desc.Width * 4;
        std::memcpy(to, from, static_cast<std::size_t>(desc.Width) * 4);
        for (UINT x = 0; x < desc.Width; ++x) to[x * 4 + 3] = 0xFF;
    }
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) &&
        SUCCEEDED(factory->CreateStream(&stream)) && SUCCEEDED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) &&
        SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) &&
        SUCCEEDED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) && SUCCEEDED(encoder->CreateNewFrame(&frame, &options)) &&
        SUCCEEDED(frame->Initialize(options.Get())) && SUCCEEDED(frame->SetSize(desc.Width, desc.Height)) &&
        SUCCEEDED(frame->SetPixelFormat(&format)) && IsEqualGUID(format, GUID_WICPixelFormat32bppBGRA) &&
        SUCCEEDED(frame->WritePixels(desc.Height, desc.Width * 4, static_cast<UINT>(pixels.size()), pixels.data())) &&
        SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit()))
        ok = true;
    context->Unmap(staging.Get(), 0);
    return ok;
}

}  // namespace blueward::ui
