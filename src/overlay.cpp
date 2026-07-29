#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "overlay.h"
#include "log.h"

LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_KEYDOWN:
        if (wparam == VK_ESCAPE) PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

HWND CreateOverlayWindow(HINSTANCE hInstance, int width, int height, bool fullscreen) {
    WNDCLASSW wc = {};
    wc.lpfnWndProc = OverlayWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"DesktopFXOverlay";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc);

    Log::Write("window size: %dx%d fullscreen=%d", width, height, fullscreen);

    int x = 0, y = 0;
    DWORD exStyle = WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;

    HWND hwnd = CreateWindowExW(
        exStyle,
        L"DesktopFXOverlay", L"DesktopFX",
        WS_POPUP,
        x, y, width, height,
        nullptr, nullptr, hInstance, nullptr
    );

    if (!hwnd) {
        Log::Write("CreateWindowExW failed: GLE=%d", GetLastError());
        return nullptr;
    }

    // Make the layered window fully opaque
    SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    Log::Write("overlay HWND=0x%p visible=%d", hwnd, IsWindowVisible(hwnd));

    // Set WDA_EXCLUDEFROMCAPTURE
    BOOL affinitySet = SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE);
    Log::Write("SetWindowDisplayAffinity: %d (GLE=%d)", affinitySet, GetLastError());

    DWORD affinity = 0;
    GetWindowDisplayAffinity(hwnd, &affinity);
    Log::Write("GetWindowDisplayAffinity: 0x%08X (expected 0x%08X)", affinity, (DWORD)WDA_EXCLUDEFROMCAPTURE);

    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    LONG_PTR ext = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    Log::Write("window styles: style=0x%08lX exStyle=0x%08lX", style, ext);

    return hwnd;
}
