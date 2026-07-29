#pragma once
#include <string>
#include <ctime>
#include <windows.h>

struct AppConfig {
    bool enabled = true;
    bool hue_enabled = true; float hue_amount = 0; float hue_speed = 0.08f;
    bool hue_r_enabled = true, hue_g_enabled = true, hue_b_enabled = true;
    bool hue_mod_enabled = false;
    float hue_min_speed = 0.0f, hue_max_speed = 2.0f, hue_mod_speed = 1.0f;
    bool contrast_enabled = false; float contrast_amount = 1.0f;
    bool saturation_enabled = false; float saturation_amount = 1.0f;
    bool invert_enabled = false;
    bool grayscale_enabled = false;
    bool pixelate_enabled = false; float pixelate_size = 8.0f;
    bool glitch_enabled = false; float glitch_intensity = 0.05f;
    bool edge_enabled = false;
    bool blend_enabled = false; std::string blend_mode = "normal";
    bool chroma_enabled = false; float chroma_amount = 0.003f;
    int chroma_mode = 0; float chroma_fade_speed = 1.0f;
    bool sharp_enabled = false; float sharp_amount = 1.0f;
    bool wave_enabled = false; float wave_intensity = 0.02f; float wave_speed = 0.5f;
    float wave_distance = 1.0f;
    bool wave_x_enabled = true; bool wave_y_enabled = true;
    bool wave_shift_enabled = false; float wave_shift_amount = 0.5f; float wave_shift_speed = 1.0f;
    bool wave_rotation_enabled = false;
    float wave_rotation_min = -180.0f; float wave_rotation_max = 180.0f;
    bool trail_enabled = false; int trail_frames = 10; float trail_opacity = 0.5f;
    int trail_capture_interval = 1;
    float trail_decay_multiplier = 0.5f;
    uint64_t trail_clear_request = 0;
    bool trail_debug_colors = false;
    bool trail_additive = false; // defaults: off, difference mask is always on
    bool glow_enabled = false; float glow_intensity = 0.3f;
    float glow_speed = 0.3f; float glow_distance = 0.3f;
    bool glow_move_enabled = true;

    // Texture Breathing
    bool texture_breathing_enabled = false;
    float texture_breathing_strength = 0.3f;
    float texture_breathing_speed = 0.5f;
    float texture_breathing_scale = 2.0f;
    float texture_breathing_noise_strength = 0.5f;

    // Pareidolia
    bool pareidolia_enabled = false;
    float pareidolia_strength = 0.3f;
    int pareidolia_zone_count = 6;
    float pareidolia_min_radius = 0.05f;
    float pareidolia_max_radius = 0.2f;
    float pareidolia_emergence_speed = 0.15f;
    float pareidolia_symmetry_strength = 0.3f;
    float pareidolia_contrast_strength = 0.2f;
    bool pareidolia_debug_view = false;
};

extern AppConfig g_config;
extern CRITICAL_SECTION g_configCS;

bool LoadConfig(const std::string& path, AppConfig& cfg);
bool ConfigFileChanged(const std::string& path);
void ConfigApply(const AppConfig& cfg);
AppConfig ConfigRead();
