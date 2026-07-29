#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <atomic>
#include "overlay.h"
#include "d3d11.h"
#include "capture_winrt.h"
#include "config.h"
#include "log.h"
#include "settings.h"

static std::atomic<bool> g_overlayRunning{false};

static DWORD WINAPI OverlayThread(LPVOID param) {
    HINSTANCE hInstance = (HINSTANCE)param;
    Log::Write("overlay thread starting");

    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    HWND hwnd = CreateOverlayWindow(hInstance, sw, sh, true);
    if (!hwnd) { Log::Write("FAILED: overlay"); g_overlayRunning = false; return 1; }

    D3D11Renderer renderer;
    if (!renderer.Init(hwnd, sw, sh)) { Log::Write("FAILED: D3D11"); DestroyWindow(hwnd); g_overlayRunning = false; return 1; }

    WinRTCapture capture;
    if (!capture.Init(renderer.Device(), MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY))) {
        Log::Write("FAILED: capture"); DestroyWindow(hwnd); g_overlayRunning = false; return 1;
    }

    AppConfig cfg; ConfigApply(cfg);
    LARGE_INTEGER freq, start;
    QueryPerformanceFrequency(&freq); QueryPerformanceCounter(&start);

    // Load config
    if (LoadConfig("config.json", cfg)) ConfigApply(cfg);

    // Warmup: wait for first captured frame so SRV has valid content
    for (int r = 0; r < 120; r++) {
        if (capture.GetFrame()) { Log::Write("first frame captured"); break; }
        Sleep(1);
    }

    int fc = 0;
    LARGE_INTEGER fpsTimer = start;
    while (g_overlayRunning) {
        cfg = ConfigRead();

        if (!cfg.enabled) { Sleep(1); continue; }

        capture.GetFrame();

        ID3D11ShaderResourceView* srv = capture.GetSRV();
        if (!srv) continue;

        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        float t = (float)((double)(now.QuadPart - start.QuadPart) / freq.QuadPart);

        ShaderConstants c = {};
        c.g_time = t;
        c.g_hue_amount = cfg.hue_amount; c.g_hue_speed = cfg.hue_speed;
        c.g_hue_enabled = cfg.hue_enabled ? 1 : 0;
        c.g_contrast = cfg.contrast_amount; c.g_contrast_enabled = cfg.contrast_enabled ? 1 : 0;
        c.g_saturation = cfg.saturation_amount; c.g_saturation_enabled = cfg.saturation_enabled ? 1 : 0;
        c.g_invert_enabled = cfg.invert_enabled ? 1 : 0;
        c.g_grayscale_enabled = cfg.grayscale_enabled ? 1 : 0;
        c.g_pixelate_size = cfg.pixelate_size;
        c.g_pixelate_enabled = cfg.pixelate_enabled ? 1 : 0;
        c.g_glitch_intensity = cfg.glitch_intensity; c.g_glitch_enabled = cfg.glitch_enabled ? 1 : 0;
        c.g_edge_enabled = cfg.edge_enabled ? 1 : 0;
        c.g_chroma_enabled = cfg.chroma_enabled ? 1 : 0;
        c.g_chroma_amount = cfg.chroma_amount; c.g_chroma_mode = cfg.chroma_mode;
        c.g_chroma_fade_speed = cfg.chroma_fade_speed;
        c.g_sharp_enabled = cfg.sharp_enabled ? 1 : 0; c.g_sharp_amount = cfg.sharp_amount;
        c.g_wave_enabled = cfg.wave_enabled ? 1 : 0;
        c.g_wave_intensity = cfg.wave_intensity; c.g_wave_speed = cfg.wave_speed;
        c.g_trail_enabled = cfg.trail_enabled ? 1 : 0;
        float frames = (float)cfg.trail_frames;
        float trailDecay = frames > 0 ? powf(0.001f, 1.0f / frames) : 0.0f;
        c.g_trail_decay = trailDecay;
        c.g_trail_opacity = cfg.trail_opacity;

        static const char* MN[] = {"normal","additive","xnor","subtract","multiply","screen","difference","overlay","and","or"};
        c.g_blend_enabled = 0;
        for (int i = 0; i < 10; i++) { if (cfg.blend_mode == MN[i] && i > 0) { c.g_blend_enabled = i; break; } }

        renderer.SetShaderConstants(c);
        renderer.Draw(srv);

        if (cfg.trail_enabled) {
            renderer.SetTrailConstants(trailDecay, cfg.trail_opacity);
            renderer.DrawMotionTrail(srv);
        }

        renderer.Present();
        fc++;

        // FPS counter update every ~60 frames
        if (fc % 60 == 0) {
            LARGE_INTEGER now;
            QueryPerformanceCounter(&now);
            double elapsed = (double)(now.QuadPart - fpsTimer.QuadPart) / freq.QuadPart;
            if (elapsed >= 1.0) {
                int fps = (int)(fc / elapsed);
                wchar_t title[64];
                swprintf_s(title, L"Desktop FX - %d FPS", fps);
                SetWindowTextW(hwnd, title);
                fpsTimer = now;
                fc = 0;
            }
        }
    }

    Log::Write("overlay thread exiting");
    DestroyWindow(hwnd);
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    InitializeCriticalSection(&g_configCS);
    Log::InitInstance(hInstance);
    SetProcessDPIAware();
    g_overlayRunning = true;
    HANDLE ot = CreateThread(nullptr, 0, OverlayThread, hInstance, 0, nullptr);
    if (!ot) { Log::Write("FAILED: thread"); Log::Close(); return 1; }

    SettingsWindowParams p;
    p.hInstance = hInstance; p.overlayRunning = &g_overlayRunning;
    int r = ShowSettingsWindow(&p);

    g_overlayRunning = false;
    WaitForSingleObject(ot, 3000); CloseHandle(ot);
    DeleteCriticalSection(&g_configCS);
    Log::Write("shutdown complete"); Log::Close();
    return r;
}
