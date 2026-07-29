#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <atomic>
#include "overlay.h"
#include "d3d11.h"
#include "capture_winrt.h"
#include "config.h"
#include "log.h"
#include "settings.h"
#include "output_sharing.h"
#include "presets.h"

static std::atomic<bool> g_overlayRunning{false};

struct OverlayParams {
    HINSTANCE hInstance;
    HANDLE shm;
    HANDLE readyEvent;
    HANDLE shutdownEvent;
};

static DWORD WINAPI OverlayThread(LPVOID param) {
    OverlayParams* ipc = (OverlayParams*)param;
    HINSTANCE hInstance = ipc->hInstance;
    HANDLE shm = ipc->shm;
    HANDLE readyEvent = ipc->readyEvent;

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

    AppConfig cfg;
    ConfigApply(cfg);
    LARGE_INTEGER freq, start;
    QueryPerformanceFrequency(&freq); QueryPerformanceCounter(&start);

    if (LoadConfig("config.json", cfg)) ConfigApply(cfg);

    // Load active preset on top of config
    {
        std::string activePreset = GetActivePreset();
        if (!activePreset.empty()) {
            char cwd[MAX_PATH];
            GetCurrentDirectoryA(MAX_PATH, cwd);
            std::string path = std::string(cwd) + "\\presets\\" + activePreset + ".json";
            AppConfig presetCfg;
            if (LoadConfig(path, presetCfg)) {
                ConfigApply(presetCfg);
                Log::Write("loaded active preset '%s'", activePreset.c_str());
            }
        }
    }

    // Warmup: wait for first captured frame so SRV has valid content
    for (int r = 0; r < 120; r++) {
        if (capture.GetFrame()) { Log::Write("first frame captured"); break; }
        Sleep(1);
    }

    int fc = 0;
    LARGE_INTEGER fpsTimer = start;
    while (g_overlayRunning) {
        cfg = ConfigRead();
        ShowWindow(hwnd, cfg.enabled ? SW_SHOW : SW_HIDE);
        if (!cfg.enabled) { Sleep(1); continue; }

        capture.GetFrame();

        ID3D11ShaderResourceView* srv = capture.GetSRV();
        if (!srv) continue;

        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        float t = (float)((double)(now.QuadPart - start.QuadPart) / freq.QuadPart);

        // @@GEN_POPULATE_SHADER_BEGIN@@
        ShaderConstants c = {};
        c.g_time = t;
        c.g_hue_amount = cfg.hue_amount;
        c.g_hue_speed = cfg.hue_speed;
        c.g_hue_min_speed = cfg.hue_min_speed;
        c.g_hue_max_speed = cfg.hue_max_speed;
        c.g_hue_mod_speed = cfg.hue_mod_speed;
        c.g_hue_enabled = cfg.hue_enabled ? 1 : 0;
        c.g_hue_r_enabled = cfg.hue_r_enabled ? 1 : 0;
        c.g_hue_g_enabled = cfg.hue_g_enabled ? 1 : 0;
        c.g_hue_b_enabled = cfg.hue_b_enabled ? 1 : 0;
        c.g_hue_mod_enabled = cfg.hue_mod_enabled ? 1 : 0;
        c.g_contrast = cfg.contrast_amount;
        c.g_saturation = cfg.saturation_amount;
        c.g_contrast_enabled = cfg.contrast_enabled ? 1 : 0;
        c.g_saturation_enabled = cfg.saturation_enabled ? 1 : 0;
        c.g_invert_enabled = cfg.invert_enabled ? 1 : 0;
        c.g_grayscale_enabled = cfg.grayscale_enabled ? 1 : 0;
        c.g_pixelate_size = cfg.pixelate_size;
        c.g_pixelate_enabled = cfg.pixelate_enabled ? 1 : 0;

        // blend mode string → int
        {
            static const char* MN[] = {"normal","additive","xnor","subtract","multiply","screen","difference","overlay","and","or"};
            c.g_blend_enabled = 0;
            for (int i = 0; i < 10; i++) { if (cfg.blend_mode == MN[i] && i > 0) { c.g_blend_enabled = i; break; } }
        }
        c.g_glitch_intensity = cfg.glitch_intensity;
        c.g_glitch_enabled = cfg.glitch_enabled ? 1 : 0;
        c.g_edge_enabled = cfg.edge_enabled ? 1 : 0;
        c.g_chroma_enabled = cfg.chroma_enabled ? 1 : 0;
        c.g_chroma_amount = cfg.chroma_amount;
        c.g_chroma_mode = cfg.chroma_mode;
        c.g_chroma_fade_speed = cfg.chroma_fade_speed;
        c.g_sharp_enabled = cfg.sharp_enabled ? 1 : 0;
        c.g_sharp_amount = cfg.sharp_amount;
        c.g_wave_enabled = cfg.wave_enabled ? 1 : 0;
        c.g_wave_intensity = cfg.wave_intensity;
        c.g_wave_speed = cfg.wave_speed;
        c.g_wave_distance = cfg.wave_distance;
        c.g_wave_x_enabled = cfg.wave_x_enabled ? 1 : 0;
        c.g_wave_y_enabled = cfg.wave_y_enabled ? 1 : 0;
        c.g_wave_shift_enabled = cfg.wave_shift_enabled ? 1 : 0;
        c.g_wave_shift_amount = cfg.wave_shift_amount;
        c.g_wave_shift_speed = cfg.wave_shift_speed;
        c.g_wave_rotation_enabled = cfg.wave_rotation_enabled ? 1 : 0;
        c.g_wave_rotation_min = cfg.wave_rotation_min;
        c.g_wave_rotation_max = cfg.wave_rotation_max;
        c.g_trail_enabled = cfg.trail_enabled ? 1 : 0;
        float trailDecay = (float)cfg.trail_frames > 0 ? powf(0.001f, 1.0f / (float)cfg.trail_frames) : 0.0f;
        c.g_trail_decay = trailDecay;
        c.g_trail_opacity = cfg.trail_opacity;
        c.g_glow_enabled = cfg.glow_enabled ? 1 : 0;
        c.g_glow_intensity = cfg.glow_intensity;
        c.g_glow_speed = cfg.glow_speed;
        c.g_glow_distance = cfg.glow_distance;
        c.g_glow_move_enabled = cfg.glow_move_enabled ? 1 : 0;
        c.g_texture_breathing_enabled = cfg.texture_breathing_enabled ? 1 : 0;
        c.g_texture_breathing_strength = cfg.texture_breathing_strength;
        c.g_texture_breathing_speed = cfg.texture_breathing_speed;
        c.g_texture_breathing_scale = cfg.texture_breathing_scale;
        c.g_texture_breathing_noise_strength = cfg.texture_breathing_noise_strength;
        c.g_pareidolia_enabled = cfg.pareidolia_enabled ? 1 : 0;
        c.g_pareidolia_strength = cfg.pareidolia_strength;
        c.g_pareidolia_zone_count = cfg.pareidolia_zone_count;
        c.g_pareidolia_min_radius = cfg.pareidolia_min_radius;
        c.g_pareidolia_max_radius = cfg.pareidolia_max_radius;
        c.g_pareidolia_emergence_speed = cfg.pareidolia_emergence_speed;
        c.g_pareidolia_symmetry_strength = cfg.pareidolia_symmetry_strength;
        c.g_pareidolia_contrast_strength = cfg.pareidolia_contrast_strength;
        c.g_pareidolia_debug_view = cfg.pareidolia_debug_view ? 1 : 0;
        c.g_pad3 = 0; c.g_pad4 = 0;
// @@GEN_POPULATE_SHADER_END@@

        renderer.SetShaderConstants(c);

        // Apply temporal config every frame (dirty-checked inside, cheap when unchanged)
        bool temporalReady = renderer.ConfigureTemporalHistory(
            cfg.trail_enabled, cfg.trail_frames,
            cfg.trail_capture_interval, cfg.trail_decay_multiplier,
            cfg.trail_clear_request,
            cfg.trail_debug_colors, cfg.trail_additive);

        if (temporalReady) {
            renderer.DrawToProcessedTarget(srv);
            if (renderer.CheckTemporalCapture())
                renderer.CaptureTemporalFrame(renderer.GetProcessedTexture());
            renderer.DrawTemporalComposite(renderer.GetProcessedSRV());
        } else if (cfg.trail_enabled) {
            // Temporal is enabled but not ready (allocating, first frame, etc.)
            // Fall back to normal rendering so screen is not blank
            renderer.Draw(srv);
        } else {
            // Original non-temporal path
            renderer.Draw(srv);
        }

        // Old recursive trail kept compiled but unused; enable for comparison testing:
        // if (cfg.trail_enabled) {
        //     renderer.SetTrailConstants(trailDecay, cfg.trail_opacity);
        //     renderer.DrawMotionTrail(srv);
        // }

        // Send frame to output window process (BEFORE Present — backbuffer is valid)
        if (shm) {
            ID3D11Resource* backRes = nullptr;
            renderer.RTV()->GetResource(&backRes);
            if (backRes) {
                ID3D11Texture2D* bb = nullptr;
                if (SUCCEEDED(backRes->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&bb))) {
                    WriteFrameToShared(shm, renderer.Context(), bb, sw, sh);
                    bb->Release();
                    SignalFrameReady(readyEvent);
                } else { static bool w = false; if (!w) { w = true; Log::Write("WriteToShared: QI failed"); } }
                backRes->Release();
            } else { static bool w = false; if (!w) { w = true; Log::Write("WriteToShared: GetResource returned null"); } }
        } else { static bool w = false; if (!w) { w = true; Log::Write("WriteToShared: shm is null"); } }

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
    // Auto-detect project root: if running from build/, go up one level
    char exeDirs[MAX_PATH];
    GetModuleFileNameA(nullptr, exeDirs, MAX_PATH);
    char* ls = strrchr(exeDirs, '\\');
    if (ls) {
        *ls = '\0';
        char* sl = strrchr(exeDirs, '\\');
        if (sl && _stricmp(sl + 1, "build") == 0) {
            *sl = '\0';
            SetCurrentDirectoryA(exeDirs);
        }
    }

    InitializeCriticalSection(&g_configCS);
    Log::InitInstance(hInstance);
    SetProcessDPIAware();

    // Create shared memory for output window
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    HANDLE shm = CreateSharedMemory(sw, sh, nullptr);
    HANDLE readyEvent = CreateEventA(nullptr, FALSE, FALSE, "DesktopFX_FrameReady");
    HANDLE shutdownEvent = CreateEventA(nullptr, TRUE, FALSE, "DesktopFX_Shutdown");

    // Launch output window process
    char exePath[MAX_PATH];
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    char* p = strrchr(exePath, '\\');
    if (p) *p = 0;
    strcat_s(exePath, "\\desktop-fx-window.exe");

    PROCESS_INFORMATION pi = {};
    STARTUPINFOA si = {sizeof(si)};
    if (CreateProcessA(exePath, nullptr, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        Log::Write("output window process launched");
        CloseHandle(pi.hThread);
    } else {
        Log::Write("output window process not found, skip");
    }

    OverlayParams params;
    params.hInstance = hInstance;
    params.shm = shm;
    params.readyEvent = readyEvent;
    params.shutdownEvent = shutdownEvent;

    g_overlayRunning = true;
    HANDLE ot = CreateThread(nullptr, 0, OverlayThread, &params, 0, nullptr);
    if (!ot) { Log::Write("FAILED: thread"); Log::Close(); return 1; }

    SettingsWindowParams sp;
    sp.hInstance = hInstance; sp.overlayRunning = &g_overlayRunning;
    ShowSettingsWindow(&sp);

    g_overlayRunning = false;
    WaitForSingleObject(ot, 3000); CloseHandle(ot);

    SignalShutdown(shutdownEvent);
    if (pi.hProcess) { WaitForSingleObject(pi.hProcess, 2000); CloseHandle(pi.hProcess); }
    if (shm) CloseHandle(shm);
    if (readyEvent) CloseHandle(readyEvent);
    if (shutdownEvent) CloseHandle(shutdownEvent);

    DeleteCriticalSection(&g_configCS);
    Log::Write("shutdown complete"); Log::Close();
    return 0;
}
