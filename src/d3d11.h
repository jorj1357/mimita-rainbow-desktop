#pragma once
#include <d3d11.h>
#include <cmath>
#include "shaders.h"

class D3D11Renderer {
public:
    ~D3D11Renderer();
    bool Init(HWND hwnd, int width, int height);
    bool InitOBSSwapChain(HWND obsHwnd);
    void SetShaderConstants(const ShaderConstants& constants);
    void Draw(ID3D11ShaderResourceView* srv);
    void SetTrailConstants(float decay, float opacity);
    void DrawMotionTrail(ID3D11ShaderResourceView* currentSrv);
    ID3D11ShaderResourceView* GetTrailOutputSRV();
    void DrawTestPattern();
    void Present();
    void CopyToOBS();
    void PresentBoth();

    ID3D11Device*        Device()       { return device_; }
    ID3D11DeviceContext* Context()      { return ctx_; }
    ID3D11RenderTargetView* RTV()       { return rtv_; }

    // ── Temporal history (circular buffer, alongside existing trail) ──
    static int ComputeEffectiveHistoryCount(int requestedFrames, int width, int height) {
        if (requestedFrames <= 0) return 0;
        if (width <= 0 || height <= 0) return 0;
        constexpr int64_t kBudget = 512LL * 1024 * 1024;
        constexpr int kMax = 1024;
        constexpr int kBPP = 4;
        uint64_t fb = (uint64_t)width * (uint64_t)height * (uint64_t)kBPP;
        if (fb == 0) return 0;
        int fromReq = (requestedFrames > kMax) ? kMax : requestedFrames;
        if (fromReq <= 0) return 0;
        int fromBud = (int)((uint64_t)kBudget / fb);
        if (fromBud < 1) return 0;
        if (fromBud > kMax) fromBud = kMax;
        return (fromReq < fromBud) ? fromReq : fromBud;
    }
    bool AllocateTemporalHistory(int requestedFrames);
    void ReleaseTemporalHistory();
    bool ResizeTemporalHistory(int requestedFrames);
    void ClearTemporalHistory();
    bool CaptureTemporalFrame(ID3D11Texture2D* processedFrame);
    int  TemporalHistoryCount() const     { return hist_count_; }
    int  TemporalHistoryCapacity() const  { return hist_capacity_; }
    int64_t TemporalHistoryBytes() const  { return hist_total_bytes_; }

    // ── Temporal render-and-composite (Phase 2) ──
    static float CalcTemporalOpacity(int age, float initial, float multiplier) {
        if (age < 0) return 0.0f;
        float raw = initial * powf(multiplier, (float)age);
        return isfinite(raw) ? raw : 0.0f;
    }
    static float CalcTemporalOpacitySaturated(int age, float initial, float multiplier) {
        float raw = CalcTemporalOpacity(age, initial, multiplier);
        return (raw < 0.0f) ? 0.0f : (raw > 1.0f) ? 1.0f : raw;
    }
    void DrawToProcessedTarget(ID3D11ShaderResourceView* srv);
    bool CheckTemporalCapture();
    void DrawTemporalComposite(ID3D11ShaderResourceView* currentFrameSRV);
    ID3D11Texture2D* GetProcessedTexture() const { return hist_processed_tex_; }
    ID3D11ShaderResourceView* GetProcessedSRV() const { return hist_processed_srv_; }
    void SetTemporalCaptureInterval(int interval) {
        hist_capture_interval_ = (interval > 0) ? interval : 1;
        if (hist_capture_counter_ > hist_capture_interval_)
            hist_capture_counter_ = hist_capture_interval_;
    }
    // Apply config values, reallocating only if needed. Returns true if enabled+allocated.
    bool ConfigureTemporalHistory(bool enabled, int requestedFrames,
                                   int captureInterval, float decayMultiplier,
                                   uint64_t clearRequest,
                                   bool debugColors = false, bool additive = false);

private:
    ID3D11Device*           device_ = nullptr;
    ID3D11DeviceContext*    ctx_ = nullptr;
    IDXGISwapChain*         swap_chain_ = nullptr;
    ID3D11RenderTargetView* rtv_ = nullptr;
    ID3D11Buffer*           vertex_buffer_ = nullptr;
    ID3D11Buffer*           index_buffer_ = nullptr;
    int                     width_ = 0;
    int                     height_ = 0;

    // OBS shadow swap chain
    IDXGISwapChain*         obs_swap_chain_ = nullptr;
    ID3D11Texture2D*        obs_backbuffer_ = nullptr;

    // Motion trail: ping-pong accumulation
    ID3D11Texture2D* trail_tex_a_ = nullptr;
    ID3D11Texture2D* trail_tex_b_ = nullptr;
    ID3D11ShaderResourceView* trail_srv_a_ = nullptr;
    ID3D11ShaderResourceView* trail_srv_b_ = nullptr;
    ID3D11RenderTargetView*   trail_rtv_a_ = nullptr;
    ID3D11RenderTargetView*   trail_rtv_b_ = nullptr;
    int trail_ping_ = 0; // 0 or 1

    CompiledShaders shaders_;

    // ── Temporal history resources (alongside old trail, not replacing it) ──
    static constexpr int      kTemporalMaxFrames = 1024;
    ID3D11Texture2D**         hist_textures_ = nullptr;
    ID3D11ShaderResourceView** hist_srvs_ = nullptr;
    int                       hist_capacity_ = 0;
    int                       hist_count_ = 0;
    int                       hist_write_idx_ = 0;
    int                       hist_width_ = 0;
    int                       hist_height_ = 0;
    uint64_t                  hist_stored_seq_[kTemporalMaxFrames] = {0};
    uint64_t                  hist_capture_seq_ = 0;
    int                       hist_requested_frames_ = 0;
    int                       hist_effective_frames_ = 0;
    int64_t                   hist_total_bytes_ = 0;

    // ── Phase 2: processed-FX target, compositing resources ──
    ID3D11Texture2D*          hist_processed_tex_ = nullptr;
    ID3D11ShaderResourceView* hist_processed_srv_ = nullptr;
    ID3D11RenderTargetView*   hist_processed_rtv_ = nullptr;
    ID3D11BlendState*         hist_blend_state_ = nullptr;
    int                       hist_capture_interval_ = 1;
    int                       hist_capture_counter_ = 1; // start at 1 → first frame captures
    float                     hist_decay_multiplier_ = 0.5f;
    bool                      hist_enabled_ = false;
    bool                      hist_temporal_ready_ = false;
    uint64_t                  hist_last_clear_gen_ = 0;
    bool                      hist_debug_enabled_ = false;
    bool                      hist_additive_ = false;
    ID3D11BlendState*         hist_additive_blend_ = nullptr;
};
