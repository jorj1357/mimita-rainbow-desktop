#pragma once
#include <d3d11.h>

struct WinRTCaptureImpl;

class WinRTCapture {
public:
    WinRTCapture();
    ~WinRTCapture();
    bool Init(ID3D11Device* device, HMONITOR monitor);
    ID3D11Texture2D* GetFrame();
    ID3D11ShaderResourceView* GetSRV() const { return captureSRV_; }
    int Width() const { return width_; }
    int Height() const { return height_; }

private:
    WinRTCaptureImpl* impl_ = nullptr;
    ID3D11Texture2D* captureTexture_ = nullptr;
    ID3D11ShaderResourceView* captureSRV_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    bool initialized_ = false;
};
