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

    // Temporal: processed-FX render target
    D3D11_TEXTURE2D_DESC ptd = {};
    ptd.Width = width; ptd.Height = height; ptd.MipLevels = 1; ptd.ArraySize = 1;
    ptd.Format = DXGI_FORMAT_B8G8R8A8_UNORM; ptd.SampleDesc.Count = 1;
    ptd.Usage = D3D11_USAGE_DEFAULT;
    ptd.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    if (FAILED(device_->CreateTexture2D(&ptd, nullptr, &hist_processed_tex_))) {
        Log::Write("Temporal: failed to create processed target"); return false;
    }
    if (FAILED(device_->CreateShaderResourceView(hist_processed_tex_, nullptr, &hist_processed_srv_))) {
        Log::Write("Temporal: failed to create processed SRV"); return false;
    }
    if (FAILED(device_->CreateRenderTargetView(hist_processed_tex_, nullptr, &hist_processed_rtv_))) {
        Log::Write("Temporal: failed to create processed RTV"); return false;
    }

    // Temporal: alpha blend state for compositing
    D3D11_BLEND_DESC blendDesc = {};
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ZERO;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(device_->CreateBlendState(&blendDesc, &hist_blend_state_))) {
        Log::Write("Temporal: failed to create blend state"); return false;
    }

    // Temporal: additive blend state (for diagnostic mode)
    D3D11_BLEND_DESC addBlend = {};
    addBlend.RenderTarget[0].BlendEnable = TRUE;
    addBlend.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    addBlend.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
    addBlend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    addBlend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ZERO;
    addBlend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
    addBlend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    addBlend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(device_->CreateBlendState(&addBlend, &hist_additive_blend_))) {
        Log::Write("Temporal: failed to create additive blend state"); return false;
    }

    // Allocate default temporal history (10 frames, will be overridden by config)
    AllocateTemporalHistory(10);

    Log::Write("D3D11 init OK");
    return true;
}

bool D3D11Renderer::InitOBSSwapChain(HWND obsHwnd) {
    if (!device_ || !obsHwnd) return false;

    IDXGIFactory2* factory = nullptr;
    HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory2), (void**)&factory);
    if (FAILED(hr)) { Log::HR("CreateDXGIFactory1 (OBS)", hr); return false; }

    DXGI_SWAP_CHAIN_DESC1 scd = {};
    scd.Width = width_; scd.Height = height_;
    scd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    scd.SampleDesc.Count = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.BufferCount = 2;
    scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

    IDXGISwapChain1* ts = nullptr;
    hr = factory->CreateSwapChainForHwnd(device_, obsHwnd, &scd, nullptr, nullptr, &ts);
    if (FAILED(hr)) {
        scd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD; scd.BufferCount = 1;
        hr = factory->CreateSwapChainForHwnd(device_, obsHwnd, &scd, nullptr, nullptr, &ts);
    }
    factory->Release();
    if (FAILED(hr) || !ts) { Log::HR("CreateSwapChainForHwnd (OBS)", hr); return false; }

    obs_swap_chain_ = ts;

    hr = obs_swap_chain_->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&obs_backbuffer_);
    if (FAILED(hr)) { Log::HR("OBS GetBuffer", hr); return false; }

    Log::Write("OBS shadow swap chain created");
    return true;
}

D3D11Renderer::~D3D11Renderer() {
    if (ctx_) ctx_->ClearState();
    ReleaseTemporalHistory();
    ReleaseShaders(shaders_);
    auto rel = [](auto*& p) { if (p) p->Release(); p = nullptr; };
    rel(index_buffer_); rel(vertex_buffer_); rel(rtv_);
    rel(obs_backbuffer_); rel(obs_swap_chain_);
    rel(swap_chain_); rel(ctx_); rel(device_);
    rel(trail_tex_a_); rel(trail_tex_b_);
    rel(trail_srv_a_); rel(trail_srv_b_);
    rel(trail_rtv_a_); rel(trail_rtv_b_);
    rel(hist_processed_tex_); rel(hist_processed_srv_); rel(hist_processed_rtv_);
    rel(hist_blend_state_); rel(hist_additive_blend_);
}

void D3D11Renderer::SetShaderConstants(const ShaderConstants& c) {
    D3D11_MAPPED_SUBRESOURCE m;
    if (SUCCEEDED(ctx_->Map(shaders_.constant_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
        size_t copySize = (sizeof(c) < 256) ? sizeof(c) : 256;
        memcpy(m.pData, &c, copySize);
        if (copySize < 192) memset((BYTE*)m.pData + copySize, 0, 192 - copySize);
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

void D3D11Renderer::CopyToOBS() {
    if (!obs_swap_chain_ || !obs_backbuffer_) return;

    // Copy main backbuffer content to OBS backbuffer
    ID3D11Resource* mainRes = nullptr;
    rtv_->GetResource(&mainRes);
    if (mainRes) {
        ctx_->CopyResource(obs_backbuffer_, mainRes);
        mainRes->Release();
    }
}

void D3D11Renderer::PresentBoth() {
    // Present overlay
    HRESULT hr = swap_chain_->Present(0, 0);
    if (FAILED(hr)) { static bool w = false; if (!w) { Log::HR("Present (overlay)", hr); w = true; } }

    // Present OBS shadow
    if (obs_swap_chain_) {
        hr = obs_swap_chain_->Present(0, 0);
        if (FAILED(hr)) { static bool w2 = false; if (!w2) { Log::HR("Present (OBS)", hr); w2 = true; } }
    }
}

void D3D11Renderer::Present() {
    HRESULT hr = swap_chain_->Present(0, 0);
    if (FAILED(hr)) { static bool w = false; if (!w) { Log::HR("Present", hr); w = true; } }
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

ID3D11ShaderResourceView* D3D11Renderer::GetTrailOutputSRV() {
    return trail_ping_ == 0 ? trail_srv_b_ : trail_srv_a_;
}

// ── Temporal render-and-composite (Phase 2) ──

bool D3D11Renderer::ConfigureTemporalHistory(bool enabled, int requestedFrames,
                                              int captureInterval, float decayMultiplier,
                                              uint64_t clearRequest,
                                              bool debugColors, bool additive) {
    hist_enabled_ = enabled;
    hist_debug_enabled_ = debugColors;
    hist_additive_ = additive;

    if (!enabled) {
        ClearTemporalHistory();
        hist_temporal_ready_ = false;
        return false;
    }

    // Handle clear request
    if (clearRequest != hist_last_clear_gen_) {
        hist_last_clear_gen_ = clearRequest;
        ClearTemporalHistory();
        hist_capture_counter_ = 1; // force capture next frame
    }

    // Set capture interval
    SetTemporalCaptureInterval(captureInterval);

    // Set decay multiplier
    hist_decay_multiplier_ = (decayMultiplier >= 0.0f) ? decayMultiplier : 0.0f;

    // Allocate or reallocate history if needed
    int eff = ComputeEffectiveHistoryCount(requestedFrames, width_, height_);
    if (eff <= 0) {
        if (hist_capacity_ > 0) ReleaseTemporalHistory();
        Log::Write("Temporal: disabled (eff=0 req=%d res=%dx%d)", requestedFrames, width_, height_);
        hist_temporal_ready_ = false;
        return false;
    }

    if (hist_capacity_ != eff || hist_width_ != width_ || hist_height_ != height_) {
        ReleaseTemporalHistory();
        if (!AllocateTemporalHistory(requestedFrames)) {
            Log::Write("Temporal: allocation failed");
            hist_temporal_ready_ = false;
            return false;
        }
    }

    // Ensure history is allocated (AllocateTemporalHistory also clears)
    if (hist_capacity_ <= 0) {
        if (!AllocateTemporalHistory(requestedFrames)) {
            hist_temporal_ready_ = false;
            return false;
        }
    }

    hist_temporal_ready_ = true;

    // Log only when config changes (not every frame)
    static bool configLogged = false;
    if (!configLogged) {
        configLogged = true;
        Log::Write("TEMPORAL CONFIG enabled=%d reqFrames=%d effFrames=%d "
                   "captureEvery=%d decay=%.2f alloc=%d count=%d",
                   enabled, requestedFrames, hist_effective_frames_,
                   hist_capture_interval_, hist_decay_multiplier_,
                   hist_capacity_, hist_count_);
    }

    return true;
}

void D3D11Renderer::DrawToProcessedTarget(ID3D11ShaderResourceView* srv) {
    D3D11_VIEWPORT vp = {0,0,(float)width_,(float)height_,0,1};
    ctx_->RSSetViewports(1, &vp);
    ctx_->OMSetRenderTargets(1, &hist_processed_rtv_, nullptr);
    float black[] = {0,0,0,0};
    ctx_->ClearRenderTargetView(hist_processed_rtv_, black);
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
    // Unbind processed RTV so CopyResource from this texture is valid
    ctx_->OMSetRenderTargets(0, nullptr, nullptr);
}

bool D3D11Renderer::CheckTemporalCapture() {
    if (hist_capacity_ <= 0) return false;

    // Always capture first frame when history is empty
    if (hist_count_ == 0) {
        hist_capture_counter_ = hist_capture_interval_;
        return true;
    }

    hist_capture_counter_--;
    if (hist_capture_counter_ <= 0) {
        hist_capture_counter_ = hist_capture_interval_;
        return true;
    }
    return false;
}

void D3D11Renderer::DrawTemporalComposite(ID3D11ShaderResourceView* currentFrameSRV) {
    if (hist_count_ <= 0 || !shaders_.temporalPs || !shaders_.temporal_buffer) return;

    float dims[2] = {(float)width_, (float)height_};
    D3D11_VIEWPORT vp = {0, 0, dims[0], dims[1], 0, 1};
    UINT stride = 20, off = 0;

    // ── Step 1: Draw current live frame to backbuffer ──
    ctx_->RSSetViewports(1, &vp);
    ctx_->OMSetRenderTargets(1, &rtv_, nullptr);
    float black[] = {0,0,0,0};
    ctx_->ClearRenderTargetView(rtv_, black);
    ctx_->IASetVertexBuffers(0,1,&vertex_buffer_,&stride,&off);
    ctx_->IASetIndexBuffer(index_buffer_,DXGI_FORMAT_R32_UINT,0);
    ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx_->IASetInputLayout(shaders_.input_layout);
    ctx_->VSSetShader(shaders_.vs,nullptr,0);
    ctx_->PSSetShader(shaders_.ps,nullptr,0);
    ctx_->PSSetConstantBuffers(0,1,&shaders_.constant_buffer);
    ctx_->PSSetSamplers(0,1,&shaders_.sampler_state);
    ctx_->PSSetShaderResources(0,1,&currentFrameSRV);
    ctx_->DrawIndexed(6,0,0);

    // ── Step 2: Overlay older historical frames with difference mask ──
    // Use the processed frame SRV as t1 (current comparison texture)
    // The current frame is NOT in the history composite; only age >= 1 is drawn
    ctx_->PSSetShader(shaders_.temporalPs,nullptr,0);
    ctx_->PSSetConstantBuffers(0,1,&shaders_.temporal_buffer);
    // Bind current frame as t1 (will stay bound for all history draws)
    ID3D11ShaderResourceView* srvs[2] = {nullptr, currentFrameSRV};
    ctx_->PSSetShaderResources(0, 2, srvs);

    // Choose blend mode (source-over alpha by default, additive for diag)
    ID3D11BlendState* activeBlend = hist_additive_ ? hist_additive_blend_ : hist_blend_state_;
    ctx_->OMSetBlendState(activeBlend, nullptr, 0xffffffff);

    // Debug age colors
    const float dbgColors[6][3] = {
        {0,1,0},{1,0,0},{0,0.3f,1},{1,1,0},{1,0,1},{0,1,1}
    };
    // Difference mask params
    const float diffThreshold = 0.03f;
    const float diffSoftness = 0.08f;

    // Iterate history age 1 → age N (skip newest, it's identical to current frame)
    for (int i = 1; i < hist_count_; i++) {
        int idx = (hist_write_idx_ - 1 - i + hist_capacity_) % hist_capacity_;

        uint64_t ageVal = hist_capture_seq_ - hist_stored_seq_[idx] - 1;
        int safeAge = (ageVal < 1000000ULL) ? (int)ageVal : 999999;
        float opacity = CalcTemporalOpacitySaturated(safeAge, 1.0f, hist_decay_multiplier_);

        int ci = safeAge % 6;
        int debugOn = hist_debug_enabled_ ? 1 : 0;

        D3D11_MAPPED_SUBRESOURCE m;
        if (SUCCEEDED(ctx_->Map(shaders_.temporal_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
            float* d = (float*)m.pData;
            d[0] = opacity;
            d[1] = diffThreshold;
            d[2] = diffSoftness;
            d[3] = (float)debugOn;
            d[4] = dbgColors[ci][0];
            d[5] = dbgColors[ci][1];
            d[6] = dbgColors[ci][2];
            d[7] = 0;
            ctx_->Unmap(shaders_.temporal_buffer, 0);
        }

        // t0 = history frame, t1 stays bound as current frame
        srvs[0] = hist_srvs_[idx];
        ctx_->PSSetShaderResources(0, 2, srvs);
        ctx_->DrawIndexed(6, 0, 0);
    }

    // Unbind SRVs
    ID3D11ShaderResourceView* nullSrvs[2] = {nullptr, nullptr};
    ctx_->PSSetShaderResources(0, 2, nullSrvs);

    // Restore default blend state
    ctx_->OMSetBlendState(nullptr, nullptr, 0xffffffff);

    // Restore the main PS for next frame
    ctx_->PSSetShader(shaders_.ps, nullptr, 0);

    if (hist_count_ > 0) {
        static int logCount = 0;
        if (++logCount <= 5) {
            int newestIdx = (hist_write_idx_ - 1 + hist_capacity_) % hist_capacity_;
            int oldestIdx = (hist_write_idx_ - hist_count_ + hist_capacity_) % hist_capacity_;
            uint64_t newestAge = hist_capture_seq_ - hist_stored_seq_[newestIdx] - 1;
            uint64_t oldestAge = hist_capture_seq_ - hist_stored_seq_[oldestIdx] - 1;
            Log::Write("TEMPORAL DRAW count=%d newestAge=%llu oldestAge=%llu",
                       hist_count_, (unsigned long long)newestAge, (unsigned long long)oldestAge);
        }
    }
}

// ── Temporal history (circular buffer of individual processed frames) ──

static constexpr int64_t kTemporalBudgetBytes = 512LL * 1024 * 1024;
static constexpr int      kTemporalBPP = 4;  // B8G8R8A8_UNORM

bool D3D11Renderer::AllocateTemporalHistory(int requestedFrames) {
    ReleaseTemporalHistory();

    hist_effective_frames_ = ComputeEffectiveHistoryCount(requestedFrames, width_, height_);
    if (hist_effective_frames_ <= 0) {
        Log::Write("TemporalHistory: disabled (req=%d res=%dx%d)",
                   requestedFrames, width_, height_);
        hist_requested_frames_ = requestedFrames;
        return false;
    }

    hist_capacity_ = hist_effective_frames_;
    hist_textures_ = new ID3D11Texture2D*[hist_capacity_]();
    hist_srvs_ = new ID3D11ShaderResourceView*[hist_capacity_]();
    hist_width_ = width_;
    hist_height_ = height_;

    D3D11_TEXTURE2D_DESC td = {};
    td.Width = width_;
    td.Height = height_;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    for (int i = 0; i < hist_capacity_; i++) {
        HRESULT hr = device_->CreateTexture2D(&td, nullptr, &hist_textures_[i]);
        if (FAILED(hr)) {
            Log::HR("TemporalHistory: CreateTexture2D", hr);
            ReleaseTemporalHistory();
            return false;
        }
        D3D11_SHADER_RESOURCE_VIEW_DESC sv = {};
        sv.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        sv.Texture2D.MipLevels = 1;
        hr = device_->CreateShaderResourceView(hist_textures_[i], &sv, &hist_srvs_[i]);
        if (FAILED(hr)) {
            Log::HR("TemporalHistory: CreateSRV", hr);
            ReleaseTemporalHistory();
            return false;
        }
    }

    ClearTemporalHistory();

    hist_total_bytes_ = (int64_t)hist_capacity_ * (int64_t)width_ * (int64_t)height_ * kTemporalBPP;

    Log::Write("TemporalHistory: alloc'd %d/%d frames %dx%d (%.1f MB)",
               hist_capacity_, requestedFrames, width_, height_,
               hist_total_bytes_ / (1024.0 * 1024.0));

    hist_requested_frames_ = requestedFrames;
    return true;
}

void D3D11Renderer::ReleaseTemporalHistory() {
    if (hist_srvs_) {
        for (int i = 0; i < hist_capacity_; i++)
            if (hist_srvs_[i]) { hist_srvs_[i]->Release(); hist_srvs_[i] = nullptr; }
        delete[] hist_srvs_;
        hist_srvs_ = nullptr;
    }
    if (hist_textures_) {
        for (int i = 0; i < hist_capacity_; i++)
            if (hist_textures_[i]) { hist_textures_[i]->Release(); hist_textures_[i] = nullptr; }
        delete[] hist_textures_;
        hist_textures_ = nullptr;
    }
    hist_capacity_ = 0;
    hist_count_ = 0;
    hist_write_idx_ = 0;
    hist_capture_seq_ = 0;
    hist_width_ = 0;
    hist_height_ = 0;
    hist_requested_frames_ = 0;
    hist_effective_frames_ = 0;
    hist_total_bytes_ = 0;
    Log::Write("TemporalHistory: released");
}

bool D3D11Renderer::ResizeTemporalHistory(int requestedFrames) {
    ReleaseTemporalHistory();
    return AllocateTemporalHistory(requestedFrames);
}

void D3D11Renderer::ClearTemporalHistory() {
    hist_count_ = 0;
    hist_write_idx_ = 0;
    hist_capture_seq_ = 0;
    Log::Write("TemporalHistory: cleared (capacity=%d)", hist_capacity_);
}

bool D3D11Renderer::CaptureTemporalFrame(ID3D11Texture2D* processedFrame) {
    if (!processedFrame || hist_capacity_ <= 0 || !hist_textures_) return false;

    int slot = hist_write_idx_;
    ctx_->CopyResource(hist_textures_[slot], processedFrame);

    hist_stored_seq_[slot] = hist_capture_seq_++;

    hist_write_idx_ = (slot + 1) % hist_capacity_;

    if (hist_count_ < hist_capacity_) hist_count_++;

    // Log first capture
    if (hist_count_ == 1) {
        Log::Write("TEMPORAL FIRST CAPTURE slot=%d count=%d capacity=%d",
                   slot, hist_count_, hist_capacity_);
    }

    return true;
}
