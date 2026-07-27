#include "output_sharing.h"
#include "log.h"
#include <cstdio>

static const char* MEM_NAME = "DesktopFX_FrameMemory";

HANDLE CreateSharedMemory(int width, int height, const char* name) {
    if (!name) name = MEM_NAME;
    size_t totalSize = sizeof(SharedFrame) + (size_t)width * height * 4;
    HANDLE mapping = CreateFileMappingA(
        INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
        (totalSize >> 32) & 0xFFFFFFFF, totalSize & 0xFFFFFFFF, name);
    if (!mapping) {
        Log::Write("CreateFileMapping failed: GLE=%d", GetLastError());
        return nullptr;
    }
    SharedFrame* sf = (SharedFrame*)MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, totalSize);
    if (sf) {
        sf->width = width;
        sf->height = height;
        sf->stride = width * 4;
        UnmapViewOfFile(sf);
    }
    Log::Write("shared memory created: %dx%d (%zu bytes)", width, height, totalSize);
    return mapping;
}

void WriteFrameToShared(HANDLE mapping, ID3D11DeviceContext* ctx,
                         ID3D11Texture2D* srcTexture, int w, int h) {
    if (!mapping || !ctx || !srcTexture) return;

    ID3D11Device* device = nullptr;
    ctx->GetDevice(&device);
    if (!device) return;

    // Cache staging texture across calls
    static thread_local ID3D11Texture2D* s_staging = nullptr;
    static thread_local int s_lastW = 0, s_lastH = 0;
    if (!s_staging || s_lastW != w || s_lastH != h) {
        if (s_staging) s_staging->Release();
        s_lastW = w; s_lastH = h;
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM; td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_STAGING; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        device->CreateTexture2D(&td, nullptr, &s_staging);
    }
    device->Release();
    if (!s_staging) return;

    ctx->CopyResource(s_staging, srcTexture);

    D3D11_MAPPED_SUBRESOURCE mapped;
    if (FAILED(ctx->Map(s_staging, 0, D3D11_MAP_READ, 0, &mapped))) return;

    size_t totalSize = sizeof(SharedFrame) + (size_t)w * h * 4;
    SharedFrame* sf = (SharedFrame*)MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, totalSize);
    if (sf) {
        sf->width = w; sf->height = h; sf->stride = mapped.RowPitch;
        BYTE* src = (BYTE*)mapped.pData;
        BYTE* dst = sf->pixels;
        for (int y = 0; y < h; y++) {
            memcpy(dst, src, w * 4);
            src += mapped.RowPitch;
            dst += w * 4;
        }
        UnmapViewOfFile(sf);
    }

    ctx->Unmap(s_staging, 0);
}

void SignalFrameReady(HANDLE event) {
    if (event) SetEvent(event);
}

void SignalShutdown(HANDLE event) {
    if (event) SetEvent(event);
}

HANDLE OpenSharedMemory(const char* name) {
    if (!name) name = MEM_NAME;
    return OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, name);
}

ID3D11Texture2D* ReadFrameToTexture(HANDLE mapping, ID3D11Device* device) {
    if (!mapping || !device) return nullptr;

    SharedFrame* sf = (SharedFrame*)MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    if (!sf) return nullptr;

    int w = sf->width, h = sf->height;
    if (w <= 0 || h <= 0) { UnmapViewOfFile(sf); return nullptr; }

    D3D11_TEXTURE2D_DESC td = {};
    td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM; td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    ID3D11Texture2D* tex = nullptr;
    HRESULT hr = device->CreateTexture2D(&td, nullptr, &tex);
    if (FAILED(hr)) { UnmapViewOfFile(sf); return nullptr; }

    td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    ID3D11Texture2D* staging = nullptr;
    hr = device->CreateTexture2D(&td, nullptr, &staging);
    if (FAILED(hr)) { tex->Release(); UnmapViewOfFile(sf); return nullptr; }

    ID3D11DeviceContext* ctx = nullptr;
    device->GetImmediateContext(&ctx);
    if (!ctx) { staging->Release(); tex->Release(); UnmapViewOfFile(sf); return nullptr; }

    D3D11_MAPPED_SUBRESOURCE m;
    if (SUCCEEDED(ctx->Map(staging, 0, D3D11_MAP_WRITE, 0, &m))) {
        BYTE* src = sf->pixels;
        BYTE* dst = (BYTE*)m.pData;
        for (int y = 0; y < h; y++) {
            memcpy(dst, src, w * 4);
            src += w * 4;
            dst += m.RowPitch;
        }
        ctx->Unmap(staging, 0);
    }
    ctx->CopyResource(tex, staging);
    ctx->Release();
    staging->Release();
    UnmapViewOfFile(sf);
    return tex;
}

bool WaitForFrame(HANDLE readyEvent, HANDLE shutdownEvent, DWORD timeoutMs) {
    HANDLE handles[2] = {readyEvent, shutdownEvent};
    DWORD result = WaitForMultipleObjects(2, handles, FALSE, timeoutMs);
    return (result == WAIT_OBJECT_0);
}
