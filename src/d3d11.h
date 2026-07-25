#pragma once
#include <d3d11.h>
#include "shaders.h"

class D3D11Renderer {
public:
    ~D3D11Renderer();
    bool Init(HWND hwnd, int width, int height);
    void SetShaderConstants(const ShaderConstants& constants);
    void Draw(ID3D11ShaderResourceView* srv);
    void SetTrailConstants(float decay, float opacity);
    void DrawMotionTrail(ID3D11ShaderResourceView* currentSrv);
    ID3D11ShaderResourceView* GetTrailOutputSRV();
    void DrawTestPattern();
    void Present();

    ID3D11Device*        Device()       { return device_; }
    ID3D11DeviceContext* Context()      { return ctx_; }

private:
    ID3D11Device*           device_ = nullptr;
    ID3D11DeviceContext*    ctx_ = nullptr;
    IDXGISwapChain*         swap_chain_ = nullptr;
    ID3D11RenderTargetView* rtv_ = nullptr;
    ID3D11Buffer*           vertex_buffer_ = nullptr;
    ID3D11Buffer*           index_buffer_ = nullptr;
    int                     width_ = 0;
    int                     height_ = 0;

    // Motion trail: ping-pong accumulation
    ID3D11Texture2D* trail_tex_a_ = nullptr;
    ID3D11Texture2D* trail_tex_b_ = nullptr;
    ID3D11ShaderResourceView* trail_srv_a_ = nullptr;
    ID3D11ShaderResourceView* trail_srv_b_ = nullptr;
    ID3D11RenderTargetView*   trail_rtv_a_ = nullptr;
    ID3D11RenderTargetView*   trail_rtv_b_ = nullptr;
    int trail_ping_ = 0; // 0 or 1

    CompiledShaders shaders_;
};
