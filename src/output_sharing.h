#pragma once
#include <windows.h>
#include <d3d11.h>

struct SharedFrame {
    int width;
    int height;
    int stride;
    int reserved;
    unsigned char pixels[1]; // variable-length data follows
};

// Creator side (main process)
HANDLE CreateSharedMemory(int width, int height, const char* name);
void WriteFrameToShared(HANDLE mapping, ID3D11DeviceContext* ctx,
                         ID3D11Texture2D* srcTexture, int w, int h);
void SignalFrameReady(HANDLE event);
void SignalShutdown(HANDLE event);

// Reader side (child process)
HANDLE OpenSharedMemory(const char* name);
ID3D11Texture2D* ReadFrameToTexture(HANDLE mapping, ID3D11Device* device);
bool WaitForFrame(HANDLE readyEvent, HANDLE shutdownEvent, DWORD timeoutMs);
