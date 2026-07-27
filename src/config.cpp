#include "config.h"
#include "log.h"
#include <fstream>
#include "../third_party/json.hpp"
using json = nlohmann::json;

AppConfig g_config;
CRITICAL_SECTION g_configCS;

static std::time_t GetLastWriteTime(const std::string& path) {
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &d)) return 0;
    ULARGE_INTEGER li; li.LowPart = d.ftLastWriteTime.dwLowDateTime;
    li.HighPart = d.ftLastWriteTime.dwHighDateTime; return li.QuadPart;
}
static std::time_t s_last_write = 0;

void ConfigApply(const AppConfig& cfg) {
    EnterCriticalSection(&g_configCS);
    g_config = cfg;
    LeaveCriticalSection(&g_configCS);
    static int _ca = 0; if (++_ca <= 5)
        Log::Write("ConfigApply #%d: enabled=%d hue=%d", _ca, cfg.enabled, cfg.hue_enabled);
}

AppConfig ConfigRead() {
    AppConfig ret;
    EnterCriticalSection(&g_configCS);
    ret = g_config;
    LeaveCriticalSection(&g_configCS);
    return ret;
}

bool LoadConfig(const std::string& path, AppConfig& cfg) {
    std::ifstream f(path); if (!f.is_open()) return false;
    try {
        json j; f >> j;
        cfg.enabled = j.value("enabled", true);
        auto& e = j["effects"];

        auto gb = [&](const char* n, const char* k, auto def) -> decltype(def) {
            return e.value(n, json::object()).value(k, def);
        };

        // @@GEN_LOAD_CONFIG_BEGIN@@
        cfg.hue_amount = gb("hue","amount", 0.0f);
        cfg.hue_speed = gb("hue","speed", 0.08f);
        cfg.hue_min_speed = gb("hue","min_speed", 0.0f);
        cfg.hue_max_speed = gb("hue","max_speed", 2.0f);
        cfg.hue_mod_speed = gb("hue","mod_speed", 1.0f);
        cfg.hue_enabled = gb("hue","enabled", false);
        cfg.hue_r_enabled = gb("hue","r_enabled", true);
        cfg.hue_g_enabled = gb("hue","g_enabled", true);
        cfg.hue_b_enabled = gb("hue","b_enabled", true);
        cfg.hue_mod_enabled = gb("hue","mod_enabled", false);
        cfg.contrast_amount = gb("contrast","amount", 1.0f);
        cfg.saturation_amount = gb("saturation","amount", 1.0f);
        cfg.contrast_enabled = gb("contrast","enabled", false);
        cfg.saturation_enabled = gb("saturation","enabled", false);
        cfg.invert_enabled = gb("invert","enabled", false);
        cfg.grayscale_enabled = gb("grayscale","enabled", false);
        cfg.pixelate_size = gb("pixelate","block_size", 8.0f);
        cfg.pixelate_enabled = gb("pixelate","enabled", false);
        std::string bm = gb("blend_mode","mode", std::string("normal"));
        cfg.blend_enabled = 0;
        if (bm == "normal") cfg.blend_enabled = 0;
        if (bm == "additive") cfg.blend_enabled = 1;
        if (bm == "xnor") cfg.blend_enabled = 2;
        if (bm == "subtract") cfg.blend_enabled = 3;
        if (bm == "multiply") cfg.blend_enabled = 4;
        if (bm == "screen") cfg.blend_enabled = 5;
        if (bm == "difference") cfg.blend_enabled = 6;
        if (bm == "overlay") cfg.blend_enabled = 7;
        if (bm == "and") cfg.blend_enabled = 8;
        if (bm == "or") cfg.blend_enabled = 9;
        cfg.blend_mode = bm;
        cfg.glitch_intensity = gb("glitch","intensity", 0.05f);
        cfg.glitch_enabled = gb("glitch","enabled", false);
        cfg.edge_enabled = gb("edge_detect","enabled", false);
        cfg.chroma_enabled = gb("chromatic_aberration","enabled", false);
        cfg.chroma_amount = gb("chromatic_aberration","amount", 0.003f);
        std::string cm = gb("chromatic_aberration","mode", std::string("off"));
        cfg.chroma_mode = cm == "off" ? 0 : cm == "static" ? 1 : cm == "fade" ? 2 : 0;
        cfg.chroma_fade_speed = gb("chromatic_aberration","fade_speed", 1.0f);
        cfg.sharp_enabled = gb("sharpness","enabled", false);
        cfg.sharp_amount = gb("sharpness","amount", 1.0f);
        cfg.wave_enabled = gb("screen_wave","enabled", false);
        cfg.wave_intensity = gb("screen_wave","intensity", 0.02f);
        cfg.wave_speed = gb("screen_wave","speed", 0.5f);
        cfg.wave_distance = gb("screen_wave","distance", 1.0f);
        cfg.wave_x_enabled = gb("screen_wave","x_enabled", true);
        cfg.wave_y_enabled = gb("screen_wave","y_enabled", true);
        cfg.wave_shift_enabled = gb("screen_wave","shift_enabled", false);
        cfg.wave_shift_amount = gb("screen_wave","shift_amount", 0.5f);
        cfg.wave_shift_speed = gb("screen_wave","shift_speed", 1.0f);
        cfg.wave_rotation_enabled = gb("screen_wave","rotation_enabled", false);
        cfg.wave_rotation_min = gb("screen_wave","rotation_min", -180.0f);
        cfg.wave_rotation_max = gb("screen_wave","rotation_max", 180.0f);
        cfg.trail_enabled = gb("motion_trail","enabled", false);
        cfg.trail_opacity = gb("motion_trail","opacity", 0.5f);
        cfg.glow_enabled = gb("glow","enabled", false);
        cfg.glow_intensity = gb("glow","intensity", 0.3f);
        cfg.glow_speed = gb("glow","speed", 0.3f);
        cfg.glow_distance = gb("glow","distance", 0.3f);
        cfg.glow_move_enabled = gb("glow","move_enabled", true);
        cfg.trail_frames = (int)gb("motion_trail","frames", 10);
// @@GEN_LOAD_CONFIG_END@@

        cfg.trail_capture_interval = (int)gb("stop_motion","capture_interval", 1);
        cfg.trail_decay_multiplier = gb("stop_motion","decay_multiplier", 0.5f);
        cfg.trail_debug_colors = gb("stop_motion","debug_colors", false);
        cfg.trail_additive = gb("stop_motion","additive", false);
        // trail_clear_request is NOT loaded from config (one-shot command)

        s_last_write = GetLastWriteTime(path);
    } catch (...) { return false; }
    return true;
}

bool ConfigFileChanged(const std::string& path) {
    std::time_t t = GetLastWriteTime(path);
    if (t != s_last_write && t != 0) { s_last_write = t; return true; }
    return false;
}
