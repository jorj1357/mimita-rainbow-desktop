#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <string>
#include <vector>
#include <fstream>
#include <cstdlib>
#include <ctime>
#include "../third_party/json.hpp"
#include "settings.h"
#include "config.h"
#include "log.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

using json = nlohmann::json;

static HWND g_hwnd = nullptr;
static std::atomic<bool>* g_overlayRunning = nullptr;
static bool g_suppressWrites = false;
static bool g_stopMotionReady = false;  // stop_motion controls exist

static HFONT g_font = nullptr;
static HBRUSH g_blackBrush = nullptr;

struct LayoutItem {
    std::string id, type, label;
    int x = 0, y = 0, w = 200;
    float def = 0, smax = 5;
    std::vector<std::string> options;
    float wave_distance_def = 1, wave_distance_smax = 10;
    float wave_shift_def = 0.5, wave_shift_smax = 5;
    float wave_shift_speed_def = 1, wave_shift_speed_smax = 5;
    float wave_rot_min_def = -180, wave_rot_max_def = 180;
    int trail_frames_def = 10;
    int trail_interval_def = 1;
    float trail_decay_def = 0.5f;
};

static std::vector<LayoutItem> g_layout;
static std::string g_layoutPath = "layout.json";
static std::time_t g_layoutLastWrite = 0;

static std::time_t GetWriteTime(const std::string& path) {
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &d)) return 0;
    ULARGE_INTEGER li;
    li.LowPart = d.ftLastWriteTime.dwLowDateTime;
    li.HighPart = d.ftLastWriteTime.dwHighDateTime;
    return li.QuadPart;
}

static bool ParseLayout(const std::string& path, std::vector<LayoutItem>& items) {
    items.clear();
    std::ifstream f(path);
    if (!f.is_open()) return false;
    try {
        json j; f >> j;
        for (auto& c : j["controls"]) {
            LayoutItem li;
            li.type = c.value("type", "");
            li.id = c.value("id", "");
            li.label = c.value("label", "");
            li.x = c.value("x", 14);
            li.y = c.value("y", 0);
            li.w = c.value("w", 200);
            li.def = c.value("def", 0.0f);
            li.smax = c.value("smax", 5.0f);
            if (c.count("options"))
                for (auto& o : c["options"]) li.options.push_back(o);
            if (c.count("distance")) {
                li.wave_distance_def = c["distance"].value("def", 1.0f);
                li.wave_distance_smax = c["distance"].value("smax", 10.0f);
            }
            if (c.count("shift")) {
                li.wave_shift_def = c["shift"].value("def", 0.5f);
                li.wave_shift_smax = c["shift"].value("smax", 5.0f);
                li.wave_shift_speed_def = c["shift"].value("speed_def", 1.0f);
                li.wave_shift_speed_smax = c["shift"].value("speed_smax", 5.0f);
            }
            if (c.count("rotation")) {
                li.wave_rot_min_def = c["rotation"].value("min_def", -180.0f);
                li.wave_rot_max_def = c["rotation"].value("max_def", 180.0f);
            }
            if (c.count("frames")) li.trail_frames_def = c["frames"].value("def", 10);
            li.trail_interval_def = c.value("interval_def", 1);
            li.trail_decay_def = c.value("decay_def", 0.5f);
            items.push_back(li);
        }
        return true;
    } catch (nlohmann::json::exception& e) {
        Log::Write("ParseLayout JSON error: %s", e.what());
        return false;
    } catch (std::exception& e) {
        Log::Write("ParseLayout error: %s", e.what());
        return false;
    } catch (...) {
        Log::Write("ParseLayout unknown error");
        return false;
    }
}

static const char* PROP_FIELD = "FX_Field";
static const char* PROP_CFG = "FX_CfgKey";   // config.json key name
static const char* PROP_EDIT_PAIR = "FX_EditPair";  // paired edit for a slider

static void SetFieldProp(HWND hwnd, const std::string& field) {
    SetPropA(hwnd, PROP_FIELD, (HANDLE)_strdup(field.c_str()));
}
static std::string GetFieldProp(HWND hwnd) {
    char* s = (char*)GetPropA(hwnd, PROP_FIELD);
    return s ? std::string(s) : "";
}
static void SetCfgKey(HWND hwnd, const std::string& key) {
    SetPropA(hwnd, PROP_CFG, (HANDLE)_strdup(key.c_str()));
}
static std::string GetCfgKey(HWND hwnd) {
    char* s = (char*)GetPropA(hwnd, PROP_CFG);
    return s ? std::string(s) : "";
}

static std::string GetClassNameStr(HWND hwnd) {
    wchar_t buf[32]; GetClassNameW(hwnd, buf, 32);
    char abuf[64]; wcstombs(abuf, buf, 64);
    return std::string(abuf);
}

// Get all edit values by field name
static float GetEditValue(const std::string& field) {
    HWND child = GetWindow(g_hwnd, GW_CHILD);
    while (child) {
        if (GetClassNameStr(child) == "Edit" && GetFieldProp(child) == field) {
            wchar_t buf[64]; GetWindowTextW(child, buf, 64);
            return (float)_wtof(buf);
        }
        child = GetNextWindow(child, GW_HWNDNEXT);
    }
    return 0;
}

static bool GetCheckValue(const std::string& field) {
    HWND child = GetWindow(g_hwnd, GW_CHILD);
    while (child) {
        if (GetClassNameStr(child) == "Button" && GetFieldProp(child) == field) {
            LRESULT st = SendMessage(child, BM_GETCHECK, 0, 0);
            return st == BST_CHECKED;
        }
        child = GetNextWindow(child, GW_HWNDNEXT);
    }
    return false;
}

static int GetComboSel(const std::string& field) {
    HWND child = GetWindow(g_hwnd, GW_CHILD);
    while (child) {
        if (GetClassNameStr(child).find("Combo") != std::string::npos && GetFieldProp(child) == field)
            return (int)SendMessage(child, CB_GETCURSEL, 0, 0);
        child = GetNextWindow(child, GW_HWNDNEXT);
    }
    return 0;
}

static void WriteConfig() {
    if (!g_hwnd) return;

    // Load existing config to preserve values when controls haven't been set
    json existingJ;
    bool hasExisting = false;
    {
        std::ifstream f("config.json");
        if (f.is_open()) { try { f >> existingJ; hasExisting = true; } catch (...) {} }
    }

    json j;
    // Preserve enabled from existing file, falling back to control state
    bool masterChecked = GetCheckValue("master");
    if (hasExisting) {
        bool existingEnabled = existingJ.value("enabled", false);
        j["enabled"] = masterChecked ? true : existingEnabled;
    } else {
        j["enabled"] = masterChecked;
    }
    j["panic_key"] = "ctrl+shift+alt+k";

    // @@GEN_WRITE_CONFIG_BEGIN@@
        j["effects"]["hue"]["amount"] = GetEditValue("hue");
        j["effects"]["hue"]["speed"] = GetEditValue("hue");
        j["effects"]["hue"]["min_speed"] = GetEditValue("hue_minspeed");
        j["effects"]["hue"]["max_speed"] = GetEditValue("hue_maxspeed");
        j["effects"]["hue"]["mod_speed"] = GetEditValue("hue_modspeed");
        j["effects"]["hue"]["enabled"] = GetCheckValue("hue");
        j["effects"]["hue"]["r_enabled"] = GetCheckValue("hue_r");
        j["effects"]["hue"]["g_enabled"] = GetCheckValue("hue_g");
        j["effects"]["hue"]["b_enabled"] = GetCheckValue("hue_b");
        j["effects"]["hue"]["mod_enabled"] = GetCheckValue("hue_mod");
        j["effects"]["contrast"]["amount"] = GetEditValue("contrast");
        j["effects"]["saturation"]["amount"] = GetEditValue("saturation");
        j["effects"]["contrast"]["enabled"] = GetCheckValue("contrast");
        j["effects"]["saturation"]["enabled"] = GetCheckValue("saturation");
        j["effects"]["invert"]["enabled"] = GetCheckValue("invert");
        j["effects"]["grayscale"]["enabled"] = GetCheckValue("grayscale");
        j["effects"]["pixelate"]["block_size"] = GetEditValue("pixelate");
        j["effects"]["pixelate"]["enabled"] = GetCheckValue("pixelate");
        int sel = GetComboSel("blend");
        static const char* MN[] = {"normal","additive","xnor","subtract","multiply","screen","difference","overlay","and","or"};
        if (sel >= 0 && sel < 10) {
            j["effects"]["blend_mode"]["enabled"] = sel > 0;
            j["effects"]["blend_mode"]["mode"] = MN[sel];
        }
        j["effects"]["glitch"]["intensity"] = GetEditValue("glitch");
        j["effects"]["glitch"]["enabled"] = GetCheckValue("glitch");
        j["effects"]["edge_detect"]["enabled"] = GetCheckValue("edge");
        j["effects"]["chromatic_aberration"]["enabled"] = GetCheckValue("chroma");
        j["effects"]["chromatic_aberration"]["amount"] = GetEditValue("chroma");
        j["effects"]["chromatic_aberration"]["fade_speed"] = 1.0;
        j["effects"]["sharpness"]["enabled"] = GetCheckValue("sharp");
        j["effects"]["sharpness"]["amount"] = GetEditValue("sharp");
        j["effects"]["screen_wave"]["enabled"] = GetCheckValue("wave");
        j["effects"]["screen_wave"]["intensity"] = GetEditValue("wave");
        j["effects"]["screen_wave"]["speed"] = GetEditValue("wave_speed");
        j["effects"]["screen_wave"]["distance"] = GetEditValue("wave_dist");
        j["effects"]["screen_wave"]["x_enabled"] = GetCheckValue("wave_x");
        j["effects"]["screen_wave"]["y_enabled"] = GetCheckValue("wave_y");
        j["effects"]["screen_wave"]["shift_enabled"] = GetCheckValue("wave_shift");
        j["effects"]["screen_wave"]["shift_amount"] = GetEditValue("wave_shamt");
        j["effects"]["screen_wave"]["shift_speed"] = GetEditValue("wave_shspd");
        j["effects"]["screen_wave"]["rotation_enabled"] = GetCheckValue("wave_rot");
        j["effects"]["screen_wave"]["rotation_min"] = GetEditValue("wave_rotmin");
        j["effects"]["screen_wave"]["rotation_max"] = GetEditValue("wave_rotmax");
        j["effects"]["motion_trail"]["enabled"] = GetCheckValue("trail");
        j["effects"]["motion_trail"]["opacity"] = GetEditValue("trail_opacity");
        j["effects"]["glow"]["enabled"] = GetCheckValue("glow");
        j["effects"]["glow"]["intensity"] = GetEditValue("glow");
        j["effects"]["glow"]["speed"] = GetEditValue("glow_speed");
        j["effects"]["glow"]["distance"] = GetEditValue("glow_distance");
        j["effects"]["glow"]["move_enabled"] = GetCheckValue("glow_move");

        // motion_trail frames (computed into g_trail_decay, no shader field)
        int tf = (int)GetEditValue("trail_frames");
        j["effects"]["motion_trail"]["frames"] = tf;
// @@GEN_WRITE_CONFIG_END@@

    // Override trail enabled from existing file if control is unchecked
    if (hasExisting) {
        bool trailChecked = GetCheckValue("trail");
        if (!trailChecked) {
            bool existingTrail = existingJ["effects"].value("motion_trail", json::object()).value("enabled", false);
            j["effects"]["motion_trail"]["enabled"] = existingTrail;
        }
    }

    // Stop motion trail config (hand-written, skip until controls are ready)
    {
        int ci = g_stopMotionReady ? (int)GetEditValue("trail_capture_interval") : 0;
        float dm = g_stopMotionReady ? GetEditValue("trail_decay") : 0.0f;
        if (hasExisting && existingJ.contains("effects") &&
            existingJ["effects"].contains("stop_motion")) {
            auto& sm = existingJ["effects"]["stop_motion"];
            if (ci <= 0 && sm.contains("capture_interval"))
                ci = sm["capture_interval"];
            if (dm <= 0.0 && sm.contains("decay_multiplier"))
                dm = (float)(double)sm["decay_multiplier"];
        }
        ci = (ci < 1) ? 1 : ci;
        j["effects"]["stop_motion"]["capture_interval"] = ci;
        j["effects"]["stop_motion"]["decay_multiplier"] = dm;
        // Preserve any extra stop_motion fields from existing file
        if (hasExisting && existingJ.contains("effects") &&
            existingJ["effects"].contains("stop_motion")) {
            for (auto& [key, val] : existingJ["effects"]["stop_motion"].items()) {
                if (!j["effects"]["stop_motion"].contains(key)) {
                    j["effects"]["stop_motion"][key] = val;
                }
            }
        }
    }

    std::ofstream f("config.json");
    if (f.is_open()) { f << j.dump(2); f.close(); }

    AppConfig cfg;
    LoadConfig("config.json", cfg);
    ConfigApply(cfg);
}

static HWND FindEditForSlider(HWND slider) {
    std::string field = GetFieldProp(slider);
    if (field.empty()) return nullptr;
    HWND child = GetWindow(g_hwnd, GW_CHILD);
    while (child) {
        if (GetClassNameStr(child) == "Edit" && GetFieldProp(child) == field)
            return child;
        child = GetNextWindow(child, GW_HWNDNEXT);
    }
    return nullptr;
}

static void SyncSliderToEdit(HWND slider) {
    HWND edit = FindEditForSlider(slider);
    if (!edit) return;
    LRESULT pos = SendMessage(slider, TBM_GETPOS, 0, 0);
    LRESULT maxR = SendMessage(slider, TBM_GETRANGEMAX, 0, 0);
    float val = (float)pos / 100.0f;
    wchar_t buf[32];
    swprintf_s(buf, L"%.3f", val);
    SetWindowTextW(edit, buf);
}

static void SetHwndField(HWND hwnd, const std::string& field) {
    SetFieldProp(hwnd, field);
}

static HWND CreateCheck(const wchar_t* label, int x, int y, int w, const std::string& field) {
    HWND c = CreateWindowW(L"BUTTON", label, WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, x, y, w, 22, g_hwnd, nullptr, nullptr, nullptr);
    SetWindowTheme(c, L" ", L" "); SendMessage(c, WM_SETFONT, (WPARAM)g_font, 0);
    SetFieldProp(c, field); return c;
}
static HWND CreateEdit(int x, int y, int w, const wchar_t* def, const std::string& field) {
    HWND e = CreateWindowW(L"EDIT", def, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_RIGHT, x, y, w, 22, g_hwnd, nullptr, nullptr, nullptr);
    SendMessage(e, WM_SETFONT, (WPARAM)g_font, 0);
    SetFieldProp(e, field); return e;
}
static void CreateLabel(const wchar_t* text, int x, int y) {
    HWND l = CreateWindowW(L"STATIC", text, WS_CHILD | WS_VISIBLE, x, y, 200, 20, g_hwnd, nullptr, nullptr, nullptr);
    SendMessage(l, WM_SETFONT, (WPARAM)g_font, 0);
}
static HWND CreateSlider(int x, int y, int w, float val, float smax, const std::string& field) {
    HWND s = CreateWindowW(L"msctls_trackbar32", L"", WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS, x, y, w, 22, g_hwnd, nullptr, nullptr, nullptr);
    SendMessage(s, TBM_SETRANGE, TRUE, MAKELPARAM(0, (int)(smax * 100)));
    SendMessage(s, TBM_SETPOS, TRUE, (int)(val * 100));
    SetFieldProp(s, field); return s;
}

static void CreateControls(HWND parent) {
    for (auto& item : g_layout) {
        const char* t = item.type.c_str();
        int x = item.x, y = item.y;
        wchar_t label[256]; mbstowcs(label, item.label.c_str(), 256);
        const std::string& id = item.id;

        if (strcmp(t, "check") == 0) {
            CreateCheck(label, x, y, 200, id);
        } else if (strcmp(t, "hue") == 0) {
            CreateCheck(label, x, y, 60, id);
            CreateSlider(x + 65, y + 1, 120, item.def, item.smax, id);
            wchar_t def[32]; swprintf_s(def, L"%.3f", item.def);
            CreateEdit(x + 190, y, 50, def, id);
            int y2 = y + 26;
            CreateCheck(L"R", x, y2, 30, "hue_r");
            CreateCheck(L"G", x + 35, y2, 30, "hue_g");
            CreateCheck(L"B", x + 70, y2, 30, "hue_b");
            CreateCheck(L"Modulate", x + 110, y2, 75, "hue_mod");
            int y3 = y2 + 26;
            CreateLabel(L"min", x, y3);
            CreateEdit(x + 30, y3, 50, L"0", "hue_minspeed");
            CreateLabel(L"max", x + 85, y3);
            wchar_t maxd[32]; swprintf_s(maxd, L"%.2f", item.smax);
            CreateEdit(x + 115, y3, 50, maxd, "hue_maxspeed");
            CreateLabel(L"mod speed", x + 170, y3);
            wchar_t modd[32]; swprintf_s(modd, L"%.2f", 1.0f);
            CreateEdit(x + 245, y3, 50, modd, "hue_modspeed");
        } else if (strcmp(t, "row") == 0) {
            CreateCheck(label, x, y, 110, id);
            CreateSlider(x + 115, y + 1, 130, item.def, item.smax, id);
            wchar_t def[32]; swprintf_s(def, L"%.3f", item.def);
            CreateEdit(x + 250, y, 60, def, id);
        } else if (strcmp(t, "button") == 0) {
            HWND b = CreateWindowW(L"BUTTON", label, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, x, y - 2, item.w, 24, parent, nullptr, nullptr, nullptr);
            SetWindowTheme(b, L" ", L" "); SendMessage(b, WM_SETFONT, (WPARAM)g_font, 0);
        } else if (strcmp(t, "sep") == 0) {
            CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ, 10, y, 470, 2, parent, nullptr, nullptr, nullptr);
        } else if (strcmp(t, "label") == 0) {
            CreateLabel(label, x, y);
        } else if (strcmp(t, "combo") == 0) {
            CreateLabel(label, x, y);
            HWND combo = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, x + 100, y - 2, 200, 200, parent, nullptr, nullptr, nullptr);
            SendMessage(combo, WM_SETFONT, (WPARAM)g_font, 0);
            SetFieldProp(combo, id);
            for (auto& o : item.options) {
                wchar_t wopt[256]; mbstowcs(wopt, o.c_str(), 256);
                SendMessage(combo, CB_ADDSTRING, 0, (LPARAM)wopt);
            }
            SendMessage(combo, CB_SETCURSEL, 0, 0);
        } else if (strcmp(t, "wave") == 0) {
            CreateCheck(label, x, y, 110, id);
            CreateSlider(x + 115, y + 1, 100, item.def, item.smax, id);
            wchar_t defs[32]; swprintf_s(defs, L"%.3f", item.def);
            CreateEdit(x + 220, y, 50, defs, id);
            CreateLabel(L"speed", x + 275, y);
            CreateSlider(x + 320, y + 1, 80, 0.5f, 5.0f, "wave_speed");

            int y2 = y + 26;
            CreateCheck(L"X", x, y2, 30, "wave_x");
            CreateCheck(L"Y", x + 35, y2, 30, "wave_y");
            CreateCheck(L"Shift", x + 75, y2, 50, "wave_shift");
            CreateLabel(L"dist", x + 130, y2);
            CreateSlider(x + 160, y2 + 1, 80, item.wave_distance_def, item.wave_distance_smax, "wave_dist");
            wchar_t dd[32]; swprintf_s(dd, L"%.2f", item.wave_distance_def);
            CreateEdit(x + 245, y2, 50, dd, "wave_dist");

            int y3 = y2 + 26;
            CreateCheck(L"Rotation", x, y3, 75, "wave_rot");
            CreateLabel(L"min", x + 80, y3);
            wchar_t rmind[32]; swprintf_s(rmind, L"%.0f", item.wave_rot_min_def);
            CreateEdit(x + 110, y3, 50, rmind, "wave_rotmin");
            CreateLabel(L"max", x + 165, y3);
            wchar_t rmaxd[32]; swprintf_s(rmaxd, L"%.0f", item.wave_rot_max_def);
            CreateEdit(x + 195, y3, 50, rmaxd, "wave_rotmax");
            CreateLabel(L"deg", x + 250, y3);

            int y4 = y3 + 26;
            CreateLabel(L"amt", x + 55, y4);
            CreateSlider(x + 85, y4 + 1, 80, item.wave_shift_def, item.wave_shift_smax, "wave_shamt");
            wchar_t sad[32]; swprintf_s(sad, L"%.2f", item.wave_shift_def);
            CreateEdit(x + 170, y4, 50, sad, "wave_shamt");
            CreateLabel(L"spd", x + 225, y4);
            CreateSlider(x + 255, y4 + 1, 80, item.wave_shift_speed_def, item.wave_shift_speed_smax, "wave_shspd");
            wchar_t ssd[32]; swprintf_s(ssd, L"%.2f", item.wave_shift_speed_def);
            CreateEdit(x + 340, y4, 50, ssd, "wave_shspd");
        } else if (strcmp(t, "trail") == 0) {
            CreateCheck(label, x, y, 110, id);
            CreateLabel(L"frames", x + 115, y);
            HWND tf = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, x + 165, y - 2, 100, 100, parent, nullptr, nullptr, nullptr);
            SendMessage(tf, WM_SETFONT, (WPARAM)g_font, 0);
            SetFieldProp(tf, "trail_frames");
            static const wchar_t* TFL[] = {L"5",L"10",L"30",L"50",L"100"};
            for (int i = 0; i < 5; i++) SendMessage(tf, CB_ADDSTRING, 0, (LPARAM)TFL[i]);
            SendMessage(tf, CB_SETCURSEL, 1, 0);
            CreateLabel(L"opacity", x + 270, y);
            CreateSlider(x + 325, y + 1, 80, 0.5f, 1.0f, "trail_opacity");
        } else if (strcmp(t, "stop_motion") == 0) {
            // Checkbox: enabled
            CreateCheck(label, x, y, 160, id);

            int y2 = y + 26;
            // Stored Frames edit + info labels
            CreateLabel(L"Stored Frames", x, y2);
            wchar_t fdef[16]; swprintf_s(fdef, L"%d", item.trail_frames_def);
            CreateEdit(x + 115, y2, 60, fdef, "trail_frames");

            // Effective / Memory labels (updated by WriteConfig)
            wchar_t infobuf[64];
            swprintf_s(infobuf, L"Effective: %d", item.trail_frames_def);
            CreateLabel(infobuf, x + 185, y2);
            // Second info line: memory estimate (will be updated on write)
            CreateLabel(L"", x + 185, y2 + 18);

            int y3 = y2 + 26;
            CreateLabel(L"Capture Every", x, y3);
            wchar_t idef[16]; swprintf_s(idef, L"%d", item.trail_interval_def);
            CreateEdit(x + 115, y3, 50, idef, "trail_capture_interval");
            CreateLabel(L"frame(s)", x + 170, y3);

            int y4 = y3 + 26;
            CreateLabel(L"Decay Multiplier", x, y4);
            wchar_t ddef[16]; swprintf_s(ddef, L"%.2f", item.trail_decay_def);
            CreateEdit(x + 135, y4, 60, ddef, "trail_decay");

            int y5 = y4 + 30;
            // Clear History button
            HWND clearBtn = CreateWindowW(L"BUTTON", L"Clear History",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, x, y5, 120, 24,
                parent, nullptr, nullptr, nullptr);
            SetWindowTheme(clearBtn, L" ", L" ");
            SendMessage(clearBtn, WM_SETFONT, (WPARAM)g_font, 0);
            SetFieldProp(clearBtn, "trail_clear");
        }
    }
}

static void RebuildControls() {
    HWND child = GetWindow(g_hwnd, GW_CHILD);
    while (child) {
        // Free stored property strings
        char* f = (char*)GetPropA(child, PROP_FIELD); if (f) { free(f); RemovePropA(child, PROP_FIELD); }
        char* ck = (char*)GetPropA(child, PROP_CFG); if (ck) { free(ck); RemovePropA(child, PROP_CFG); }
        HWND next = GetNextWindow(child, GW_HWNDNEXT);
        DestroyWindow(child);
        child = next;
    }
    ParseLayout(g_layoutPath, g_layout);
    CreateControls(g_hwnd);
}

static LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_CREATE) {
        g_hwnd = hwnd;
        RegisterHotKey(hwnd, 2, MOD_CONTROL | MOD_SHIFT | MOD_ALT, 'K');
        // Load config from file into shared state immediately
        {
            AppConfig initCfg;
            if (LoadConfig("config.json", initCfg)) {
                ConfigApply(initCfg);
            }
        }
        SetTimer(hwnd, 1, 500, nullptr);
        return 0;
    }
    if (msg == WM_CLOSE) {
        if (g_overlayRunning) *g_overlayRunning = false;
        DestroyWindow(hwnd);
        return 0;
    }
    if (msg == WM_DESTROY) {
        g_hwnd = nullptr;
        KillTimer(hwnd, 1); UnregisterHotKey(hwnd, 2);
        PostQuitMessage(0);
        return 0;
    }
    if (msg == WM_HOTKEY && wParam == 2) {
        if (g_overlayRunning) *g_overlayRunning = false;
        return 0;
    }
    if (msg == WM_TIMER && wParam == 1) {
        std::time_t t = GetWriteTime(g_layoutPath);
        if (t != g_layoutLastWrite && t > g_layoutLastWrite && t != 0) {
            g_layoutLastWrite = t;
            RebuildControls();
            WriteConfig();
        }
        return 0;
    }
    if (msg == WM_CTLCOLORSTATIC || msg == WM_CTLCOLORBTN || msg == WM_CTLCOLOREDIT || msg == WM_CTLCOLORLISTBOX) {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, RGB(255, 255, 255));
        SetBkColor(hdc, RGB(0, 0, 0));
        SetBkMode(hdc, TRANSPARENT);
        return (LRESULT)g_blackBrush;
    }
    if (msg == WM_COMMAND) {
        int code = HIWORD(wParam);
        // Handle Clear History button (one-shot action, not a persisted config change)
        if (code == BN_CLICKED) {
            HWND hCtrl = (HWND)lParam;
            std::string field = GetFieldProp(hCtrl);
            if (field == "trail_clear") {
                EnterCriticalSection(&g_configCS);
                g_config.trail_clear_request++;
                LeaveCriticalSection(&g_configCS);
                Log::Write("GUI: Clear History button (gen=%llu)",
                           (unsigned long long)g_config.trail_clear_request);
                return 0;
            }
        }
        if (!g_suppressWrites && (code == BN_CLICKED || code == EN_CHANGE || code == CBN_SELCHANGE)) {
            WriteConfig();
            return 0;
        }
    }
    if (msg == WM_HSCROLL || msg == WM_VSCROLL) {
        HWND slider = (HWND)lParam;
        if (slider) SyncSliderToEdit(slider);
        WriteConfig();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int ShowSettingsWindow(SettingsWindowParams* params) {
    g_overlayRunning = params->overlayRunning;
    INITCOMMONCONTROLSEX icex = {sizeof(icex), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES};
    InitCommonControlsEx(&icex);

    g_font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Times New Roman");
    g_blackBrush = CreateSolidBrush(RGB(0, 0, 0));

    g_layoutLastWrite = GetWriteTime(g_layoutPath);
    if (!ParseLayout(g_layoutPath, g_layout)) {
        MessageBoxA(nullptr, "Failed to parse layout.json", "Error", MB_OK);
        return 1;
    }

    WNDCLASSW wc = {};
    wc.lpfnWndProc = SettingsWndProc;
    wc.hInstance = params->hInstance;
    wc.hbrBackground = g_blackBrush;
    wc.lpszClassName = L"DesktopFXSettingsClass";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc);

    std::ifstream f(g_layoutPath);
    int winW = 500, winH = 700;
    if (f.is_open()) {
        json j; f >> j;
        winW = j["window"].value("width", 500);
        winH = j["window"].value("height", 700);
    }

    HWND hwnd = CreateWindowExW(0, L"DesktopFXSettingsClass", L"Desktop FX",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, winW, winH,
        nullptr, nullptr, params->hInstance, nullptr);
    if (!hwnd) return 1;

    SetWindowTheme(hwnd, L" ", L" ");
    g_hwnd = hwnd;

    CreateControls(hwnd);

    // Initialize control values from config (suppress writes during init)
    {
        AppConfig existingCfg;
        if (LoadConfig("config.json", existingCfg)) {
            g_suppressWrites = true;
            HWND child = GetWindow(g_hwnd, GW_CHILD);
            while (child) {
                std::string field = GetFieldProp(child);
                if (field == "master")
                    SendMessage(child, BM_SETCHECK, existingCfg.enabled ? BST_CHECKED : BST_UNCHECKED, 0);
                else if (field == "trail")
                    SendMessage(child, BM_SETCHECK, existingCfg.trail_enabled ? BST_CHECKED : BST_UNCHECKED, 0);
                else if (field == "trail_frames") {
                    wchar_t buf[32]; swprintf_s(buf, L"%d", existingCfg.trail_frames);
                    SetWindowTextW(child, buf);
                } else if (field == "trail_capture_interval") {
                    wchar_t buf[32]; swprintf_s(buf, L"%d", existingCfg.trail_capture_interval);
                    SetWindowTextW(child, buf);
                } else if (field == "trail_decay") {
                    wchar_t buf[32]; swprintf_s(buf, L"%.3f", existingCfg.trail_decay_multiplier);
                    SetWindowTextW(child, buf);
                } else if (field == "trail_capture_interval") {
                    wchar_t buf[32]; swprintf_s(buf, L"%d", existingCfg.trail_capture_interval);
                    SetWindowTextW(child, buf);
                } else if (field == "trail_frames") {
                    wchar_t buf[32]; swprintf_s(buf, L"%d", existingCfg.trail_frames);
                    SetWindowTextW(child, buf);
                }
                child = GetNextWindow(child, GW_HWNDNEXT);
            }
            g_suppressWrites = false;
        }
    }
    g_stopMotionReady = true;
    WriteConfig();

    ShowWindow(hwnd, SW_SHOW);
    RedrawWindow(hwnd, NULL, NULL, RDW_ERASE | RDW_INVALIDATE | RDW_ALLCHILDREN);

    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    g_hwnd = nullptr;
    return 0;
}
