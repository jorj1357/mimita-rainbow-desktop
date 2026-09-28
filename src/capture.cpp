#include "capture.h"
#include "log.h"

DesktopCapture::~DesktopCapture() {
    if (staging_) staging_->Release();
    if (dup_) { dup_->ReleaseFrame(); dup_->Release(); }
    Log::Write("capture released");
}

bool DesktopCapture::Init(ID3D11Device* device) {
    device_ = device;

    IDXGIDevice* dxgi_dev = nullptr;
    device->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgi_dev);

    IDXGIAdapter* adapter = nullptr;
    dxgi_dev->GetAdapter(&adapter);
    dxgi_dev->Release();

    IDXGIOutput* output = nullptr;
    adapter->EnumOutputs(0, &output);
    adapter->Release();

    DXGI_OUTPUT_DESC desc;
    output->GetDesc(&desc);
    screen_w_ = desc.DesktopCoordinates.right - desc.DesktopCoordinates.left;
    screen_h_ = desc.DesktopCoordinates.bottom - desc.DesktopCoordinates.top;
    Log::Write("DDA output: %dx%d", screen_w_, screen_h_);

    IDXGIOutput1* out1 = nullptr;
    output->QueryInterface(__uuidof(IDXGIOutput1), (void**)&out1);
    output->Release();

    HRESULT hr = out1->DuplicateOutput(device, &dup_);
    out1->Release();
    if (FAILED(hr)) {
        Log::HR("DuplicateOutput", hr);
        return false;
    }
    Log::Write("DDA duplicate OK");

    D3D11_TEXTURE2D_DESC sd = {};
    sd.Width = screen_w_;
    sd.Height = screen_h_;
    sd.MipLevels = 1;
    sd.ArraySize = 1;
    sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.Usage = D3D11_USAGE_DEFAULT;
    sd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    hr = device->CreateTexture2D(&sd, nullptr, &staging_);
    if (FAILED(hr)) { Log::HR("CreateTexture2D", hr); return false; }

    Log::Write("capture init OK");
    return true;
}

ID3D11Texture2D* DesktopCapture::GetFrame() {
    if (!dup_) return nullptr;

    IDXGIResource* resource = nullptr;
    DXGI_OUTDUPL_FRAME_INFO info;
    HRESULT hr = dup_->AcquireNextFrame(500, &info, &resource);

    if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
        static int warn = 0;
        if (++warn == 1) Log::Write("DDA timeout (first warning)");
        return nullptr;
    }
    if (FAILED(hr)) { Log::HR("AcquireNextFrame", hr); return nullptr; }

    ID3D11Texture2D* tex = nullptr;
    resource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&tex);
    resource->Release();
    if (!tex) { dup_->ReleaseFrame(); return nullptr; }

    ID3D11DeviceContext* ctx = nullptr;
    device_->GetImmediateContext(&ctx);
    ctx->CopyResource(staging_, tex);
    ctx->Release();
    tex->Release();
    dup_->ReleaseFrame();

    return staging_;
}

void DesktopCapture::DoneWithFrame() {
}
