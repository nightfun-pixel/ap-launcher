#include "media/video.h"

#include "core/text.h"

#include "resource.h"

#include "core/errors.h"
#include "core/log.h"

#include <mfapi.h>
#include <mferror.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace blueward::media {

namespace {

UINT Even(double value) { return std::max<UINT>(2, static_cast<UINT>(std::lround(value)) & ~1u); }

constexpr DWORD kVideoStream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
constexpr DWORD kAllStreams = static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS);

constexpr int kVideoResources[] = {IDR_BG_720, IDR_BG_1080, IDR_BG_1080_60, IDR_BG_1440, IDR_BG_1440_60};

bool HasVideoResource(int resource) {
    return resource != 0 && FindResourceW(nullptr, MAKEINTRESOURCEW(resource), RT_RCDATA) != nullptr;
}

int ResolveVideoResource(int wanted) {
    if (HasVideoResource(wanted)) return wanted;
    for (const int resource : kVideoResources)
        if (HasVideoResource(resource)) return resource;
    return 0;
}

}  // namespace

bool HasEmbeddedVideo() { return ResolveVideoResource(0) != 0; }

bool VideoBackground::Start(ID3D11Device* device, const std::wstring& path, UINT maxWidth, UINT maxHeight, int resource) {
    Stop();
    if (!device) return false;
    const bool file = !path.empty() && GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
    const int resolved = file ? 0 : ResolveVideoResource(resource);
    if (!file && resolved == 0) return false;
    device_ = device;
    device_->GetImmediateContext(&context_);
    path_ = path;
    resource_ = resolved;
    maxWidth_ = maxWidth;
    maxHeight_ = maxHeight;
    {
        std::lock_guard lock(mutex_);
        stop_ = false;
        paused_ = false;
        pending_.Reset();
        stats_ = {};
    }
    thread_ = std::thread([this] { Run(); });
    return true;
}

void VideoBackground::Stop() {
    if (thread_.joinable()) {
        {
            std::lock_guard lock(mutex_);
            stop_ = true;
        }
        wake_.notify_all();
        thread_.join();
    }
    std::lock_guard lock(mutex_);
    pending_.Reset();
    frame_.Reset();
    ++generation_;
    resource_ = 0;
    context_.Reset();
    device_.Reset();
}

void VideoBackground::SetPaused(bool paused) {
    {
        std::lock_guard lock(mutex_);
        if (paused_ == paused) return;
        paused_ = paused;
    }
    wake_.notify_all();
}

VideoStats VideoBackground::Stats() const {
    std::lock_guard lock(mutex_);
    return stats_;
}

void VideoBackground::Fail(const char* where, HRESULT hr) {
    std::lock_guard lock(mutex_);
    if (!stats_.failed) {
        LogWarn(L"фоновое видео: сбой на шаге «" + Widen(where) + L"»: " + DescribeHresult(static_cast<long>(hr)) +
                L" Лаунчер рисует фон без видео.");
    }
    stats_.failed = true;
    stats_.failedAt = where;
    stats_.failedHr = static_cast<long>(hr);
}

bool VideoBackground::EnsureFrame(UINT width, UINT height) {
    if (frame_) {
        D3D11_TEXTURE2D_DESC have{};
        frame_->GetDesc(&have);
        if (have.Width == width && have.Height == height) return true;
        frame_.Reset();
    }
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(device_->CreateTexture2D(&desc, nullptr, &frame_))) return false;
    ++generation_;
    return true;
}

HRESULT VideoBackground::OpenEmbeddedReader(IMFAttributes* attributes, IMFSourceReader** reader) {
    const HRSRC info = FindResourceW(nullptr, MAKEINTRESOURCEW(resource_), RT_RCDATA);
    if (!info) return HRESULT_FROM_WIN32(ERROR_RESOURCE_TYPE_NOT_FOUND);
    const DWORD size = SizeofResource(nullptr, info);
    const HGLOBAL loaded = LoadResource(nullptr, info);
    const void* data = loaded ? LockResource(loaded) : nullptr;
    if (!data || size == 0) return E_FAIL;
    HGLOBAL block = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!block) return E_OUTOFMEMORY;
    void* copy = GlobalLock(block);
    if (!copy) {
        GlobalFree(block);
        return E_OUTOFMEMORY;
    }
    std::memcpy(copy, data, size);
    GlobalUnlock(block);
    ComPtr<IStream> stream;
    HRESULT hr = CreateStreamOnHGlobal(block, TRUE, &stream);
    if (FAILED(hr)) {
        GlobalFree(block);
        return hr;
    }
    ComPtr<IMFByteStream> bytes;
    hr = MFCreateMFByteStreamOnStream(stream.Get(), &bytes);
    if (FAILED(hr)) return hr;
    hr = MFCreateSourceReaderFromByteStream(bytes.Get(), attributes, reader);
    if (FAILED(hr)) return hr;
    stream_ = stream;
    byteStream_ = bytes;
    return S_OK;
}

void VideoBackground::Run() {
    const bool com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
    const HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_LITE);
    if (SUCCEEDED(hr)) {
        Decode();
        {
            std::lock_guard lock(mutex_);
            pending_.Reset();
        }
        byteStream_.Reset();
        stream_.Reset();
        MFShutdown();
    } else {
        Fail("MFStartup", hr);
    }
    if (com) CoUninitialize();
}

void VideoBackground::Decode() {
    ComPtr<IMFDXGIDeviceManager> manager;
    UINT token = 0;
    HRESULT hr = MFCreateDXGIDeviceManager(&token, &manager);
    if (FAILED(hr)) return Fail("MFCreateDXGIDeviceManager", hr);
    hr = manager->ResetDevice(device_.Get(), token);
    if (FAILED(hr)) return Fail("ResetDevice", hr);

    ComPtr<IMFAttributes> attributes;
    hr = MFCreateAttributes(&attributes, 3);
    if (FAILED(hr)) return Fail("MFCreateAttributes", hr);
    attributes->SetUnknown(MF_SOURCE_READER_D3D_MANAGER, manager.Get());
    attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
    attributes->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE);

    ComPtr<IMFSourceReader> reader;
    if (resource_ != 0) {
        hr = OpenEmbeddedReader(attributes.Get(), &reader);
        if (FAILED(hr)) return Fail("MFCreateSourceReaderFromByteStream", hr);
    } else {
        hr = MFCreateSourceReaderFromURL(path_.c_str(), attributes.Get(), &reader);
        if (FAILED(hr)) return Fail("MFCreateSourceReaderFromURL", hr);
    }
    reader->SetStreamSelection(kAllStreams, FALSE);
    hr = reader->SetStreamSelection(kVideoStream, TRUE);
    if (FAILED(hr)) return Fail("SetStreamSelection", hr);

    UINT nativeWidth = 0, nativeHeight = 0;
    {
        ComPtr<IMFMediaType> native;
        hr = reader->GetNativeMediaType(kVideoStream, 0, &native);
        if (FAILED(hr)) return Fail("GetNativeMediaType", hr);
        MFGetAttributeSize(native.Get(), MF_MT_FRAME_SIZE, &nativeWidth, &nativeHeight);
    }
    if (nativeWidth == 0 || nativeHeight == 0) return Fail("native size", E_FAIL);

    const double scale =
        std::min(1.0, std::max(static_cast<double>(maxWidth_) / nativeWidth, static_cast<double>(maxHeight_) / nativeHeight));
    const UINT wantWidth = Even(nativeWidth * scale), wantHeight = Even(nativeHeight * scale);
    auto setType = [&](const GUID& subtype, UINT width, UINT height) {
        ComPtr<IMFMediaType> type;
        if (FAILED(MFCreateMediaType(&type))) return E_FAIL;
        type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        type->SetGUID(MF_MT_SUBTYPE, subtype);
        if (width) MFSetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, width, height);
        return reader->SetCurrentMediaType(kVideoStream, nullptr, type.Get());
    };
    hr = setType(MFVideoFormat_ARGB32, wantWidth, wantHeight);
    if (FAILED(hr)) hr = setType(MFVideoFormat_RGB32, wantWidth, wantHeight);
    if (FAILED(hr)) hr = setType(MFVideoFormat_ARGB32, 0, 0);
    if (FAILED(hr)) return Fail("SetCurrentMediaType", hr);

    UINT outWidth = 0, outHeight = 0;
    {
        ComPtr<IMFMediaType> current;
        if (SUCCEEDED(reader->GetCurrentMediaType(kVideoStream, &current)))
            MFGetAttributeSize(current.Get(), MF_MT_FRAME_SIZE, &outWidth, &outHeight);
    }
    if (outWidth == 0 || outHeight == 0) return Fail("output size", E_FAIL);
    {
        std::lock_guard lock(mutex_);
        stats_.nativeWidth = nativeWidth;
        stats_.nativeHeight = nativeHeight;
        stats_.width = outWidth;
        stats_.height = outHeight;
    }

    using Clock = std::chrono::steady_clock;
    Clock::time_point base = Clock::now();
    LONGLONG firstStamp = 0;
    bool rebase = true;
    bool delivered = false;
    for (;;) {
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, [&] { return stop_ || !paused_ || !delivered; });
            if (stop_) return;
        }
        DWORD flags = 0;
        LONGLONG stamp = 0;
        ComPtr<IMFSample> sample;
        hr = reader->ReadSample(kVideoStream, 0, nullptr, &flags, &stamp, &sample);
        if (FAILED(hr) || (flags & MF_SOURCE_READERF_ERROR)) return Fail("ReadSample", FAILED(hr) ? hr : E_FAIL);
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
            PROPVARIANT start;
            PropVariantInit(&start);
            start.vt = VT_I8;
            start.hVal.QuadPart = 0;
            hr = reader->SetCurrentPosition(GUID_NULL, start);
            PropVariantClear(&start);
            if (FAILED(hr)) return Fail("SetCurrentPosition", hr);
            rebase = true;
            continue;
        }
        if (!sample) continue;
        if (rebase) {
            base = Clock::now();
            firstStamp = stamp;
            rebase = false;
        }
        const auto due = base + std::chrono::microseconds((stamp - firstStamp) / 10);
        if (Clock::now() > due + std::chrono::milliseconds(250)) {
            base = Clock::now();
            firstStamp = stamp;
        } else {
            std::unique_lock lock(mutex_);
            if (wake_.wait_until(lock, due, [&] { return stop_ || (paused_ && delivered); })) {
                if (stop_) return;
                rebase = true;
                continue;
            }
        }
        std::lock_guard lock(mutex_);
        if (pending_) ++stats_.dropped;
        pending_ = sample;
        pendingWidth_ = outWidth;
        pendingHeight_ = outHeight;
        ++stats_.decoded;
        delivered = true;
    }
}

bool VideoBackground::Update() {
    ComPtr<IMFSample> sample;
    UINT width = 0, height = 0;
    {
        std::lock_guard lock(mutex_);
        if (!pending_) return false;
        sample.Swap(pending_);
        width = pendingWidth_;
        height = pendingHeight_;
    }
    ComPtr<IMFMediaBuffer> buffer;
    if (!context_ || FAILED(sample->GetBufferByIndex(0, &buffer)) || !EnsureFrame(width, height)) return false;

    bool copied = false, viaCpu = false;
    ComPtr<IMFDXGIBuffer> dxgi;
    if (SUCCEEDED(buffer.As(&dxgi))) {
        ComPtr<ID3D11Texture2D> texture;
        UINT subresource = 0;
        if (SUCCEEDED(dxgi->GetResource(IID_PPV_ARGS(&texture))) && SUCCEEDED(dxgi->GetSubresourceIndex(&subresource))) {
            D3D11_TEXTURE2D_DESC desc{};
            texture->GetDesc(&desc);
            if (desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM && desc.Width >= width && desc.Height >= height) {
                const D3D11_BOX box{0, 0, 0, width, height, 1};
                context_->CopySubresourceRegion(frame_.Get(), 0, 0, 0, 0, texture.Get(), subresource, &box);
                copied = true;
            }
        }
    }
    if (!copied) {
        ComPtr<IMF2DBuffer> flat;
        if (SUCCEEDED(buffer.As(&flat))) {
            BYTE* scan0 = nullptr;
            LONG pitch = 0;
            if (SUCCEEDED(flat->Lock2D(&scan0, &pitch))) {
                std::vector<BYTE> rows(static_cast<std::size_t>(width) * 4 * height);
                for (UINT y = 0; y < height; ++y)
                    std::memcpy(rows.data() + static_cast<std::size_t>(y) * width * 4, scan0 + static_cast<ptrdiff_t>(y) * pitch,
                                static_cast<std::size_t>(width) * 4);
                flat->Unlock2D();
                context_->UpdateSubresource(frame_.Get(), 0, nullptr, rows.data(), width * 4, 0);
                copied = viaCpu = true;
            }
        }
    }
    if (!copied) return false;
    std::lock_guard lock(mutex_);
    ++stats_.shown;
    if (viaCpu) ++stats_.cpuFrames;
    return true;
}

}  // namespace blueward::media
