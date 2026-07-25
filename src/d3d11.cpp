#include "d3d11.h"
#include "log.h"
#include <dxgi1_2.h>

bool D3D11Renderer::Init(HWND hwnd, int width, int height) {
    width_ = width; height_ = height;

    IDXGIFactory2* factory = nullptr;
    HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory2), (void**)&factory);
    if (FAILED(hr)) { Log::HR("CreateDXGIFactory1", hr); return false; }

    bool found = false;
    for (int ai = 0; ai < 5; ai++) {
        IDXGIAdapter1* adapter = nullptr;
        hr = factory->EnumAdapters1(ai, &adapter);
        if (hr == DXGI_ERROR_NOT_FOUND) break;
        if (FAILED(hr)) continue;

        DXGI_ADAPTER_DESC1 desc;
        adapter->GetDesc1(&desc);
        Log::Write("trying adapter[%d]: %S", ai, desc.Description);

        ID3D11Device* td = nullptr; ID3D11DeviceContext* tc = nullptr;
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
        hr = D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags,
            &fl, 1, D3D11_SDK_VERSION, &td, nullptr, &tc);
        adapter->Release();
        if (FAILED(hr)) continue;

        DXGI_SWAP_CHAIN_DESC1 scd = {};
        scd.Width = width; scd.Height = height;
        scd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        scd.SampleDesc.Count = 1;
        scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        scd.BufferCount = 2;
        scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        scd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

        IDXGISwapChain1* ts = nullptr;
        hr = factory->CreateSwapChainForHwnd(td, hwnd, &scd, nullptr, nullptr, &ts);
        if (FAILED(hr)) {
            scd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD; scd.BufferCount = 1;
            hr = factory->CreateSwapChainForHwnd(td, hwnd, &scd, nullptr, nullptr, &ts);
        }
        if (SUCCEEDED(hr) && ts) {
            device_ = td; ctx_ = tc; swap_chain_ = ts;
            found = true;
            Log::Write("  SUCCESS: adapter[%d]", ai);
            break;
        }
        tc->Release(); td->Release();
    }
    factory->Release();
    if (!found) { Log::Write("FAILED: no adapter"); return false; }

    ID3D11Texture2D* bb = nullptr;
    hr = swap_chain_->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb);
    if (FAILED(hr)) { Log::HR("GetBuffer", hr); return false; }
    hr = device_->CreateRenderTargetView(bb, nullptr, &rtv_);
    bb->Release();
    if (FAILED(hr)) { Log::HR("CreateRTV", hr); return false; }

    float quad[] = {
        -1,-1,0,0,1,  1,-1,0,1,1,  1,1,0,1,0,  -1,1,0,0,0
    };
    UINT idx[] = {0,1,2,0,2,3};
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = sizeof(quad); bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA dd = {quad};
    if (FAILED(device_->CreateBuffer(&bd, &dd, &vertex_buffer_))) return false;
    bd.ByteWidth = sizeof(idx); bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA id = {idx};
    if (FAILED(device_->CreateBuffer(&bd, &id, &index_buffer_))) return false;

    if (!CompileShaders(device_, shaders_)) { Log::Write("Shader compile failed"); return false; }

    // Motion trail textures + RTVs
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = width; td.Height = height; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM; td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;

    auto makeTrail = [&](ID3D11Texture2D*& tex, ID3D11ShaderResourceView*& srv, ID3D11RenderTargetView*& rtv) {
        if (FAILED(device_->CreateTexture2D(&td, nullptr, &tex))) return false;
        if (FAILED(device_->CreateShaderResourceView(tex, nullptr, &srv))) return false;
        if (FAILED(device_->CreateRenderTargetView(tex, nullptr, &rtv))) return false;
        // Initialize to black
        float black[] = {0,0,0,0};
        ctx_->ClearRenderTargetView(rtv, black);
        return true;
    };
    if (!makeTrail(trail_tex_a_, trail_srv_a_, trail_rtv_a_)) return false;
    if (!makeTrail(trail_tex_b_, trail_srv_b_, trail_rtv_b_)) return false;

    // Disable culling
    D3D11_RASTERIZER_DESC rd = {};
    rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE;
    rd.FrontCounterClockwise = FALSE; rd.DepthClipEnable = TRUE;
    ID3D11RasterizerState* rs = nullptr;
    device_->CreateRasterizerState(&rd, &rs);
    if (rs) ctx_->RSSetState(rs);

    Log::Write("D3D11 init OK");
    return true;
}

D3D11Renderer::~D3D11Renderer() {
    if (ctx_) ctx_->ClearState();
    ReleaseShaders(shaders_);
    auto rel = [](auto*& p) { if (p) p->Release(); p = nullptr; };
    rel(index_buffer_); rel(vertex_buffer_); rel(rtv_);
    rel(swap_chain_); rel(ctx_); rel(device_);
    rel(trail_tex_a_); rel(trail_tex_b_);
    rel(trail_srv_a_); rel(trail_srv_b_);
    rel(trail_rtv_a_); rel(trail_rtv_b_);
}

void D3D11Renderer::SetShaderConstants(const ShaderConstants& c) {
    D3D11_MAPPED_SUBRESOURCE m;
    if (SUCCEEDED(ctx_->Map(shaders_.constant_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
        size_t copySize = (sizeof(c) < 128) ? sizeof(c) : 128;
        memcpy(m.pData, &c, copySize);
        if (copySize < 128) memset((BYTE*)m.pData + copySize, 0, 128 - copySize);
        ctx_->Unmap(shaders_.constant_buffer, 0);
    }
    static bool logged = false;
    if (!logged) { logged = true; Log::Write("sizeof(ShaderConstants)=%zu", sizeof(c)); }
}

void D3D11Renderer::Draw(ID3D11ShaderResourceView* srv) {
    D3D11_VIEWPORT vp = {0,0,(float)width_,(float)height_,0,1};
    ctx_->RSSetViewports(1, &vp);
    ctx_->OMSetRenderTargets(1, &rtv_, nullptr);
    float clear[] = {0,0,0,0};
    ctx_->ClearRenderTargetView(rtv_, clear);
    if (!srv) return;

    UINT stride = 20, off = 0;
    ctx_->IASetVertexBuffers(0,1,&vertex_buffer_,&stride,&off);
    ctx_->IASetIndexBuffer(index_buffer_,DXGI_FORMAT_R32_UINT,0);
    ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx_->IASetInputLayout(shaders_.input_layout);
    ctx_->VSSetShader(shaders_.vs,nullptr,0);
    ctx_->PSSetShader(shaders_.ps,nullptr,0);
    ctx_->PSSetConstantBuffers(0,1,&shaders_.constant_buffer);
    ctx_->PSSetSamplers(0,1,&shaders_.sampler_state);
    ctx_->PSSetShaderResources(0,1,&srv);
    ctx_->DrawIndexed(6,0,0);
}

void D3D11Renderer::SetTrailConstants(float decay, float opacity) {
    D3D11_MAPPED_SUBRESOURCE m;
    if (SUCCEEDED(ctx_->Map(shaders_.trail_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
        float* data = (float*)m.pData;
        data[0] = decay;   // g_decay
        data[1] = opacity; // g_opacity
        data[2] = 0;       // padding
        data[3] = 0;       // padding
        ctx_->Unmap(shaders_.trail_buffer, 0);
    }
}

void D3D11Renderer::DrawMotionTrail(ID3D11ShaderResourceView* currentSrv) {
    if (!shaders_.trailPs) return;

    // Read from the current accum texture (trail_ping), write to the other (1-trail_ping)
    ID3D11ShaderResourceView* accumSrv = trail_ping_ == 0 ? trail_srv_a_ : trail_srv_b_;
    ID3D11RenderTargetView* outRtv = trail_ping_ == 0 ? trail_rtv_b_ : trail_rtv_a_;

    D3D11_VIEWPORT vp = {0,0,(float)width_,(float)height_,0,1};
    ctx_->RSSetViewports(1, &vp);
    ctx_->OMSetRenderTargets(1, &outRtv, nullptr);
    float clear[] = {0,0,0,0};
    ctx_->ClearRenderTargetView(outRtv, clear);

    UINT stride = 20, off = 0;
    ctx_->IASetVertexBuffers(0,1,&vertex_buffer_,&stride,&off);
    ctx_->IASetIndexBuffer(index_buffer_,DXGI_FORMAT_R32_UINT,0);
    ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx_->IASetInputLayout(shaders_.input_layout);
    ctx_->VSSetShader(shaders_.vs,nullptr,0);
    ctx_->PSSetShader(shaders_.trailPs,nullptr,0);
    ctx_->PSSetConstantBuffers(0,1,&shaders_.trail_buffer);
    ctx_->PSSetSamplers(0,1,&shaders_.sampler_state);

    ID3D11ShaderResourceView* srvs[2] = {currentSrv, accumSrv};
    ctx_->PSSetShaderResources(0, 2, srvs);

    ctx_->DrawIndexed(6,0,0);

    // Blit trail output to main RTV for display
    ctx_->OMSetRenderTargets(1, &rtv_, nullptr);
    ctx_->Flush();

    // Copy the output to the current RTV by re-rendering or using CopyResource
    // Actually we render directly to trail FBO, then copy to main RTV
    // For simplicity, copy the trail output to the backbuffer:
    ID3D11Resource* trailRes = nullptr;
    outRtv->GetResource(&trailRes);
    ID3D11Resource* backRes = nullptr;
    rtv_->GetResource(&backRes);
    if (trailRes && backRes) {
        ctx_->CopyResource(backRes, trailRes);
    }
    if (trailRes) trailRes->Release();
    if (backRes) backRes->Release();

    // Swap ping-pong
    trail_ping_ = 1 - trail_ping_;
}

void D3D11Renderer::DrawTestPattern() {
    static ID3D11Texture2D* tex = nullptr;
    static ID3D11ShaderResourceView* srv = nullptr;
    if (!tex) {
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = 1; td.Height = 1; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM; td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_IMMUTABLE; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        UINT32 red = 0x000000FF;
        D3D11_SUBRESOURCE_DATA d = {&red, 4, 4};
        device_->CreateTexture2D(&td, &d, &tex);
        device_->CreateShaderResourceView(tex, nullptr, &srv);
    }
    ShaderConstants c = {}; c.g_time = 0;
    SetShaderConstants(c);
    Draw(srv);
    ctx_->OMSetRenderTargets(1, &rtv_, nullptr);
    Present();
}

void D3D11Renderer::Present() {
    HRESULT hr = swap_chain_->Present(0, 0);
    if (FAILED(hr)) { static bool w = false; if (!w) { Log::HR("Present", hr); w = true; } }
}

ID3D11ShaderResourceView* D3D11Renderer::GetTrailOutputSRV() {
    return trail_ping_ == 0 ? trail_srv_b_ : trail_srv_a_;
}
