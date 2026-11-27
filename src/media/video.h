#pragma once
// Looping mp4 background. A worker thread reads the clip through a Media Foundation source reader on
// the renderer's own D3D11 device, so decoding and YUV->RGB stay on the GPU, and hands the newest
// frame to the UI thread. When no loose file exists the RCDATA clip compiled into the exe is read
// through an in-memory byte stream. Stop() joins the thread and releases the reader, the streams and
// every texture.

#include <windows.h>

#include <d3d11.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <objidl.h>
#include <wrl/client.h>

#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

namespace blueward::media {

bool HasEmbeddedVideo();

struct VideoStats {
    unsigned long long decoded = 0;
    unsigned long long shown = 0;
    unsigned long long dropped = 0;
    unsigned long long cpuFrames = 0;
    UINT nativeWidth = 0, nativeHeight = 0, width = 0, height = 0;
    bool failed = false;
    long failedHr = 0;
    const char* failedAt = "";
};

class VideoBackground {
public:
    VideoBackground() = default;
    VideoBackground(const VideoBackground&) = delete;
    VideoBackground& operator=(const VideoBackground&) = delete;
    ~VideoBackground() { Stop(); }

    bool Start(ID3D11Device* device, const std::wstring& path, UINT maxWidth, UINT maxHeight, int resource = 0);
    void Stop();
    bool Running() const { return thread_.joinable(); }
    void SetPaused(bool paused);

    bool Update();
    ID3D11Texture2D* Frame() const { return frame_.Get(); }
    unsigned FrameGeneration() const { return generation_; }
    VideoStats Stats() const;

private:
    void Run();
    void Decode();
    HRESULT OpenEmbeddedReader(IMFAttributes* attributes, IMFSourceReader** reader);
    void Fail(const char* where, HRESULT hr);
    bool EnsureFrame(UINT width, UINT height);

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> frame_;
    Microsoft::WRL::ComPtr<IStream> stream_;
    Microsoft::WRL::ComPtr<IMFByteStream> byteStream_;
    unsigned generation_ = 0;
    std::wstring path_;
    int resource_ = 0;
    UINT maxWidth_ = 0, maxHeight_ = 0;

    std::thread thread_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    bool stop_ = false, paused_ = false;
    Microsoft::WRL::ComPtr<IMFSample> pending_;
    UINT pendingWidth_ = 0, pendingHeight_ = 0;
    VideoStats stats_;
};

}  // namespace blueward::media
