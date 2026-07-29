#include "config.h"
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

        cfg.hue_enabled = gb("hue","enabled", false);
        cfg.hue_amount = gb("hue","amount", 0.0f);
        cfg.hue_speed = gb("hue","speed", 2.0f);

        cfg.contrast_enabled = gb("contrast","enabled", false);
        cfg.contrast_amount = gb("contrast","amount", 1.0f);

        cfg.saturation_enabled = gb("saturation","enabled", false);
        cfg.saturation_amount = gb("saturation","amount", 1.0f);

        cfg.invert_enabled = gb("invert","enabled", false);
        cfg.grayscale_enabled = gb("grayscale","enabled", false);

        cfg.pixelate_enabled = gb("pixelate","enabled", false);
        cfg.pixelate_size = gb("pixelate","block_size", 8.0f);

        cfg.glitch_enabled = gb("glitch","enabled", false);
        cfg.glitch_intensity = gb("glitch","intensity", 0.05f);

        cfg.edge_enabled = gb("edge_detect","enabled", false);

        auto& blend = e["blend_mode"];
        cfg.blend_enabled = blend.value("enabled", false);
        cfg.blend_mode = blend.value("mode", "normal");

        cfg.chroma_enabled = gb("chromatic_aberration","enabled", false);
        cfg.chroma_amount = gb("chromatic_aberration","amount", 0.003f);
        std::string cm = gb("chromatic_aberration","mode", std::string("off"));
        cfg.chroma_mode = cm == "static" ? 1 : cm == "fade" ? 2 : 0;
        cfg.chroma_fade_speed = gb("chromatic_aberration","fade_speed", 1.0f);

        cfg.sharp_enabled = gb("sharpness","enabled", false);
        cfg.sharp_amount = gb("sharpness","amount", 1.0f);

        cfg.wave_enabled = gb("screen_wave","enabled", false);
        cfg.wave_intensity = gb("screen_wave","intensity", 0.02f);
        cfg.wave_speed = gb("screen_wave","speed", 0.5f);

        cfg.trail_enabled = gb("motion_trail","enabled", false);
        cfg.trail_frames = gb("motion_trail","frames", 10);
        cfg.trail_opacity = gb("motion_trail","opacity", 0.5f);

        s_last_write = GetLastWriteTime(path);
    } catch (...) { return false; }
    return true;
}

bool ConfigFileChanged(const std::string& path) {
    std::time_t t = GetLastWriteTime(path);
    if (t != s_last_write && t != 0) { s_last_write = t; return true; }
    return false;
}
