#pragma once
#include <d3d11.h>
#include <dxgi1_2.h>

class DesktopCapture {
public:
    ~DesktopCapture();
    bool Init(ID3D11Device* device);
    ID3D11Texture2D* GetFrame();
    void DoneWithFrame();

private:
    IDXGIOutputDuplication* dup_ = nullptr;
    ID3D11Device* device_ = nullptr;
    ID3D11Texture2D* staging_ = nullptr;
    int screen_w_ = 0;
    int screen_h_ = 0;
};
