#include "capture_winrt.h"
#include "log.h"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

#pragma comment(lib, "windowsapp.lib")

using namespace winrt;
using namespace winrt::Windows::Graphics;
using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;

struct WinRTCaptureImpl {
    Direct3D11CaptureFramePool framePool{nullptr};
    GraphicsCaptureSession session{nullptr};
};

WinRTCapture::WinRTCapture() : impl_(new WinRTCaptureImpl()) {}

WinRTCapture::~WinRTCapture() {
    if (impl_) {
        if (impl_->session) impl_->session.Close();
        if (impl_->framePool) impl_->framePool.Close();
        delete impl_;
    }
    if (captureTexture_) captureTexture_->Release();
    Log::Write("WinRT capture released");
}

bool WinRTCapture::Init(ID3D11Device* d3d11Device, HMONITOR monitor) {
    init_apartment(apartment_type::multi_threaded);

    // 1. Create IDirect3DDevice from ID3D11Device
    IDXGIDevice* dxgiDevice = nullptr;
    HRESULT hr = d3d11Device->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice);
    if (FAILED(hr)) { Log::HR("QI IDXGIDevice", hr); return false; }

    winrt::com_ptr<::IInspectable> winrtDevice;
    hr = CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice, winrtDevice.put());
    dxgiDevice->Release();
    if (FAILED(hr)) { Log::HR("CreateDirect3D11DeviceFromDXGIDevice", hr); return false; }

    auto direct3DDevice = winrtDevice.as<IDirect3DDevice>();
    Log::Write("WinRT device created");

    // 2. Get monitor info
    MONITORINFOEXW mi;
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(monitor, &mi);
    width_ = mi.rcMonitor.right - mi.rcMonitor.left;
    height_ = mi.rcMonitor.bottom - mi.rcMonitor.top;
    Log::Write("monitor: %dx%d", width_, height_);

    // 3. Create GraphicsCaptureItem from monitor via interop
    auto factory = get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
    winrt::com_ptr<ABI::Windows::Graphics::Capture::IGraphicsCaptureItem> itemInterop;
    hr = factory->CreateForMonitor(
        monitor,
        __uuidof(ABI::Windows::Graphics::Capture::IGraphicsCaptureItem),
        itemInterop.put_void()
    );
    if (FAILED(hr)) { Log::HR("CreateForMonitor", hr); return false; }

    auto captureItem = itemInterop.as<GraphicsCaptureItem>();
    Log::Write("capture item created");

    // 4. Create Direct3D11CaptureFramePool
    SizeInt32 frameSize;
    frameSize.Width = width_;
    frameSize.Height = height_;
    auto pool = Direct3D11CaptureFramePool::Create(
        direct3DDevice,
        DirectXPixelFormat::B8G8R8A8UIntNormalized,
        2,
        frameSize
    );
    impl_->framePool = pool;
    Log::Write("frame pool created (capacity=2)");

    // 5. Create capture session
    auto session = impl_->framePool.CreateCaptureSession(captureItem);
    impl_->session = session;
    session.IsCursorCaptureEnabled(false);
    Log::Write("cursor capture disabled");
    session.StartCapture();
    Log::Write("capture started");

    // 6. Create capture texture (shader resource)
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = width_;
    td.Height = height_;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    hr = d3d11Device->CreateTexture2D(&td, nullptr, &captureTexture_);
    if (FAILED(hr)) { Log::HR("CreateTexture2D", hr); return false; }

    initialized_ = true;
    Log::Write("WinRT capture init OK");
    return true;
}

ID3D11Texture2D* WinRTCapture::GetFrame() {
    if (!initialized_) return nullptr;

    auto& pool = impl_->framePool;
    auto frame = pool.TryGetNextFrame();
    if (!frame) return nullptr;

    auto surface = frame.Surface();

    // QI the WinRT surface for IDirect3DDxgiInterfaceAccess (COM interface)
    typedef ::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess DxgiAccess;
    IUnknown* abiSurface = reinterpret_cast<IUnknown*>(winrt::get_abi(surface));
    DxgiAccess* access = nullptr;
    HRESULT hr = abiSurface->QueryInterface(__uuidof(DxgiAccess), (void**)&access);
    if (FAILED(hr)) {
        Log::HR("QI DxgiInterfaceAccess", hr);
        frame.Close();
        return nullptr;
    }

    // Get the ID3D11Texture2D from the surface
    ID3D11Texture2D* frameTexture = nullptr;
    hr = access->GetInterface(__uuidof(ID3D11Texture2D), (void**)&frameTexture);
    access->Release();
    if (FAILED(hr)) {
        Log::HR("GetInterface(ID3D11Texture2D)", hr);
        frame.Close();
        return nullptr;
    }

    // Copy to our persistent texture (frame texture is tied to frame's lifetime)
    ID3D11Device* device = nullptr;
    frameTexture->GetDevice(&device);
    if (device) {
        ID3D11DeviceContext* ctx = nullptr;
        device->GetImmediateContext(&ctx);
        if (ctx) {
            ctx->CopyResource(captureTexture_, frameTexture);
            ctx->Release();
        }
        device->Release();
    }

    frameTexture->Release();
    frame.Close();

    return captureTexture_;
}
