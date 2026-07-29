#pragma once
#include <string>
#include <ctime>
#include <windows.h>

struct AppConfig {
    bool enabled = true;
    bool hue_enabled = true; float hue_amount = 0; float hue_speed = 2.0f;
    bool contrast_enabled = false; float contrast_amount = 1.0f;
    bool saturation_enabled = false; float saturation_amount = 1.0f;
    bool invert_enabled = false;
    bool grayscale_enabled = false;
    bool pixelate_enabled = false; float pixelate_size = 8.0f;
    bool glitch_enabled = false; float glitch_intensity = 0.05f;
    bool edge_enabled = false;
    bool blend_enabled = false; std::string blend_mode = "normal";
    bool chroma_enabled = false; float chroma_amount = 0.003f;
    int chroma_mode = 0; // 0=off, 1=static, 2=fade
    float chroma_fade_speed = 1.0f;
    bool sharp_enabled = false; float sharp_amount = 1.0f;
    bool wave_enabled = false; float wave_intensity = 0.02f; float wave_speed = 0.5f;
    bool trail_enabled = false; int trail_frames = 10; float trail_opacity = 0.5f;
};

// Thread-safe shared config (no file I/O race)
extern AppConfig g_config;
extern CRITICAL_SECTION g_configCS;

bool LoadConfig(const std::string& path, AppConfig& cfg);
bool ConfigFileChanged(const std::string& path);
void ConfigApply(const AppConfig& cfg);
AppConfig ConfigRead();
