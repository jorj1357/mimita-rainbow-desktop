#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi1_2.h>
#include <cstdio>
#include <fstream>

#include "output_sharing.h"

static const char* READY_EVT = "DesktopFX_FrameReady";
static const char* SHUTDOWN_EVT = "DesktopFX_Shutdown";

static void log_write(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    FILE* f = nullptr;
    fopen_s(&f, "output_window_log.txt", "a");
    if (f) {
        vfprintf(f, fmt, args);
        fprintf(f, "\n");
        fclose(f);
    }
    va_end(args);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_DESTROY: PostQuitMessage(0); return 0;
    case WM_NCHITTEST: return HTTRANSPARENT;
    }
    return DefWindowProcW(hwnd, msg, w, l);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    SetProcessDPIAware();
    log_write("=== output window starting ===");

    HANDLE readyEvent = CreateEventA(nullptr, FALSE, FALSE, READY_EVT);
    HANDLE shutdownEvent = CreateEventA(nullptr, TRUE, FALSE, SHUTDOWN_EVT);
    HANDLE shm = OpenSharedMemory(nullptr);

    log_write("readyEvent=0x%p shutdownEvent=0x%p shm=0x%p", readyEvent, shutdownEvent, shm);

    if (!shm) {
        log_write("FAILED: cannot open shared memory");
        return 1;
    }

    // Create window
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"DesktopFXOutputWin";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc);

    int w = GetSystemMetrics(SM_CXSCREEN);
    int h = GetSystemMetrics(SM_CYSCREEN);
    log_write("screen: %dx%d", w, h);

    HWND hwnd = CreateWindowExW(0, L"DesktopFXOutputWin", L"Desktop FX Output",
        WS_OVERLAPPEDWINDOW, -4000, 0, w, h, nullptr, nullptr, hInstance, nullptr);
    if (!hwnd) { log_write("FAILED: create window"); return 1; }
    ShowWindow(hwnd, SW_SHOW); UpdateWindow(hwnd);
    log_write("window created HWND=0x%p", hwnd);

    // D3D11 device
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    IDXGISwapChain1* swapChain = nullptr;

    DXGI_SWAP_CHAIN_DESC1 scd = {};
    scd.Width = w; scd.Height = h;
    scd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    scd.SampleDesc.Count = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.BufferCount = 2;
    scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
        &device, nullptr, &ctx);
    if (FAILED(hr)) {
        log_write("HARDWARE device failed: 0x%08X, trying WARP", hr);
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
            nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &ctx);
    }
    if (FAILED(hr)) { log_write("FAILED: D3D11CreateDevice 0x%08X", hr); return 1; }
    log_write("D3D11 device OK");

    IDXGIFactory2* factory = nullptr;
    CreateDXGIFactory1(__uuidof(IDXGIFactory2), (void**)&factory);
    if (factory) {
        hr = factory->CreateSwapChainForHwnd(device, hwnd, &scd, nullptr, nullptr, &swapChain);
        factory->Release();
    }
    if (!swapChain) { log_write("FAILED: swap chain"); return 1; }
    log_write("swap chain OK");

    ID3D11Texture2D* backbuffer = nullptr;
    swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backbuffer);
    ID3D11RenderTargetView* rtv = nullptr;
    if (backbuffer) {
        device->CreateRenderTargetView(backbuffer, nullptr, &rtv);
        backbuffer->Release();
    }
    if (!rtv) { log_write("FAILED: RTV"); return 1; }

    // Shaders: fullscreen quad with NO texture — solid color output
    // This proves the D3D11 pipeline works without any shared memory dependency
    ID3DBlob* vsBlob = nullptr;
    ID3DBlob* psBlob = nullptr;
    ID3D11VertexShader* vs = nullptr;
    ID3D11PixelShader* psTest = nullptr;  // for test pattern (no texture)
    ID3D11PixelShader* psFrame = nullptr; // for frame display (texture)
    ID3D11InputLayout* il = nullptr;
    ID3D11Buffer* vb = nullptr;
    ID3D11Buffer* ib = nullptr;
    ID3D11SamplerState* smp = nullptr;
    ID3D11RasterizerState* rs = nullptr;

    const char* vsSrc = R"(
        struct VSOut { float4 pos:SV_POSITION; float2 uv:TEXCOORD0; };
        VSOut vs_main(float3 pos:POSITION, float2 uv:TEXCOORD0) {
            VSOut o; o.pos = float4(pos,1); o.uv = uv; return o;
        }
    )";
    D3DCompile(vsSrc, strlen(vsSrc), nullptr, nullptr, nullptr, "vs_main", "vs_5_0", 0, 0, &vsBlob, nullptr);
    if (vsBlob) {
        device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &vs);
        device->CreateInputLayout(nullptr, 0, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &il);
        // Re-create input layout with proper elements
        D3D11_INPUT_ELEMENT_DESC lay[] = {
            {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0},
        };
        device->CreateInputLayout(lay, 2, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &il);
        vsBlob->Release();
    }
    log_write("VS: vs=0x%p il=0x%p", vs, il);

    // TEST pattern shader: solid RED (no texture needed)
    const char* psTestSrc = R"(
        float4 ps_main(float4 pos:SV_POSITION, float2 uv:TEXCOORD0) : SV_TARGET {
            return float4(1, 0, 0, 1);  // solid red
        }
    )";
    ID3DBlob* psBlobTest = nullptr;
    if (SUCCEEDED(D3DCompile(psTestSrc, strlen(psTestSrc), nullptr, nullptr, nullptr, "ps_main", "ps_5_0", 0, 0, &psBlobTest, nullptr))) {
        device->CreatePixelShader(psBlobTest->GetBufferPointer(), psBlobTest->GetBufferSize(), nullptr, &psTest);
        psBlobTest->Release();
        log_write("test PS (solid red) compiled");
    }

    // Frame display shader: samples from texture
    const char* psFrameSrc = R"(
        Texture2D tex : register(t0);
        SamplerState smp : register(s0);
        float4 ps_main(float4 pos:SV_POSITION, float2 uv:TEXCOORD0) : SV_TARGET {
            return tex.Sample(smp, uv);
        }
    )";
    ID3DBlob* psBlobFrame = nullptr;
    if (SUCCEEDED(D3DCompile(psFrameSrc, strlen(psFrameSrc), nullptr, nullptr, nullptr, "ps_main", "ps_5_0", 0, 0, &psBlobFrame, nullptr))) {
        device->CreatePixelShader(psBlobFrame->GetBufferPointer(), psBlobFrame->GetBufferSize(), nullptr, &psFrame);
        psBlobFrame->Release();
        log_write("frame PS compiled");
    }

    // Quad geometry
    float quad[] = {-1,-1,0,0,1, 1,-1,0,1,1, 1,1,0,1,0, -1,1,0,0,0};
    UINT idx[] = {0,1,2,0,2,3};
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = sizeof(quad); bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA dd = {quad};
    device->CreateBuffer(&bd, &dd, &vb);
    bd.ByteWidth = sizeof(idx); bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA id = {idx};
    device->CreateBuffer(&bd, &id, &ib);

    D3D11_SAMPLER_DESC sd = {};
    sd.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.ComparisonFunc = D3D11_COMPARISON_NEVER; sd.MaxLOD = D3D11_FLOAT32_MAX;
    device->CreateSamplerState(&sd, &smp);

    D3D11_RASTERIZER_DESC rdesc = {};
    rdesc.FillMode = D3D11_FILL_SOLID; rdesc.CullMode = D3D11_CULL_NONE;
    rdesc.FrontCounterClockwise = FALSE; rdesc.DepthClipEnable = TRUE;
    device->CreateRasterizerState(&rdesc, &rs);

    // Helper to draw a fullscreen quad with a given pixel shader
    auto drawQuad = [&](ID3D11PixelShader* pixelShader, ID3D11ShaderResourceView* texSrv = nullptr) {
        D3D11_VIEWPORT vp = {0,0,(float)w,(float)h,0,1};
        ctx->RSSetViewports(1, &vp);
        ctx->RSSetState(rs);
        ctx->OMSetRenderTargets(1, &rtv, nullptr);
        float black[] = {0,0,0,0};
        ctx->ClearRenderTargetView(rtv, black);

        UINT stride = 20, off = 0;
        ctx->IASetVertexBuffers(0,1,&vb,&stride,&off);
        ctx->IASetIndexBuffer(ib, DXGI_FORMAT_R32_UINT, 0);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->IASetInputLayout(il);
        ctx->VSSetShader(vs, nullptr, 0);
        ctx->PSSetShader(pixelShader, nullptr, 0);
        ctx->PSSetSamplers(0, 1, &smp);

        if (texSrv) ctx->PSSetShaderResources(0, 1, &texSrv);

        ctx->DrawIndexed(6, 0, 0);
        swapChain->Present(0, 0);
    };

    bool running = true;
    int frameCount = 0;
    MSG msg = {};

    log_write("entering loop: first 5 frames = RED (test pattern), then texture mode");

    while (running) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) running = false;
            TranslateMessage(&msg); DispatchMessageW(&msg);
        }
        if (!running) break;

        frameCount++;

        // Phase 1: Test pattern (first 5 frames — solid red, no shared memory needed)
        if (frameCount <= 5) {
            drawQuad(psTest);
            if (frameCount == 1) log_write("frame %d: rendered RED test pattern", frameCount);
            continue;
        }

        // Phase 2: Wait for a frame from shared memory
        HANDLE waitHandles[2] = {readyEvent, shutdownEvent};
        DWORD wr = WaitForMultipleObjects(2, waitHandles, FALSE, 5000);  // 5s timeout

        if (wr == WAIT_OBJECT_0 + 1 || wr == WAIT_FAILED) {
            log_write("frame %d: shutdown/error wr=%lu", frameCount, wr);
            running = false;
            continue;
        }
        if (wr != WAIT_OBJECT_0) {
            log_write("frame %d: wait timed out wr=%lu", frameCount, wr);
            continue;
        }

        log_write("frame %d: event signaled, reading frame", frameCount);

        // Read frame from shared memory
        ID3D11Texture2D* frameTex = ReadFrameToTexture(shm, device);
        if (!frameTex) {
            log_write("frame %d: ReadFrameToTexture returned null", frameCount);
            continue;
        }

        log_write("frame %d: ReadFrameToTexture OK tex=0x%p", frameCount, frameTex);

        // Create SRV and render
        ID3D11ShaderResourceView* srv = nullptr;
        D3D11_SHADER_RESOURCE_VIEW_DESC sv = {};
        sv.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        sv.Texture2D.MipLevels = 1;
        HRESULT srvHr = device->CreateShaderResourceView(frameTex, &sv, &srv);
        if (SUCCEEDED(srvHr)) {
            drawQuad(psFrame, srv);
            srv->Release();
            log_write("frame %d: rendered from shared memory", frameCount);
        } else {
            log_write("frame %d: CreateSRV failed 0x%08X", frameCount, srvHr);
        }

        frameTex->Release();
    }

    log_write("shutting down");

    // Cleanup
    if (rs) rs->Release();
    if (smp) smp->Release();
    if (vb) vb->Release();
    if (ib) ib->Release();
    if (il) il->Release();
    if (vs) vs->Release();
    if (psTest) psTest->Release();
    if (psFrame) psFrame->Release();
    if (rtv) rtv->Release();
    if (swapChain) swapChain->Release();
    if (ctx) ctx->Release();
    if (device) device->Release();
    if (shm) CloseHandle(shm);
    if (readyEvent) CloseHandle(readyEvent);
    if (shutdownEvent) CloseHandle(shutdownEvent);

    return 0;
}
