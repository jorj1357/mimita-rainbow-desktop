#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <string>
#include <fstream>
#include "../third_party/json.hpp"
#include "settings.h"
#include "config.h"
#include "log.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#include <uxtheme.h>

using json = nlohmann::json;
static HWND g_hwnd = nullptr;
static std::atomic<bool>* g_overlayRunning = nullptr;
static HFONT g_font = nullptr;
static HBRUSH g_blackBrush = nullptr;

static const wchar_t* BLEND_MODES[] = {
    L"Normal", L"Additive", L"XNOR", L"Subtract", L"Multiply",
    L"Screen", L"Difference", L"Overlay", L"AND", L"OR"
};
static const int BLEND_COUNT = 10;

static void WriteConfig() {
    if (!g_hwnd) return;
    json j;
    j["enabled"] = (SendMessage(GetDlgItem(g_hwnd, ID_MASTER_CHECK), BM_GETCHECK, 0, 0) == BST_CHECKED);
    j["panic_key"] = "ctrl+shift+alt+k";

    wchar_t buf[64];
    auto checked = [](int id) { return SendMessage(GetDlgItem(g_hwnd, id), BM_GETCHECK, 0, 0) == BST_CHECKED; };
    auto text = [&](int id) { GetDlgItemTextW(g_hwnd, id, buf, 64); return (float)_wtof(buf); };
    auto sliderVal = [](int id) { return (float)SendMessage(GetDlgItem(g_hwnd, id), TBM_GETPOS, 0, 0) / 100.0f; };

    j["effects"]["hue"]["enabled"] = checked(ID_HUE_CHECK);
    j["effects"]["hue"]["amount"] = text(ID_HUE_EDIT);
    j["effects"]["hue"]["speed"] = sliderVal(ID_HUE_SLIDER);

    j["effects"]["contrast"]["enabled"] = checked(ID_CONTRAST_CHECK);
    j["effects"]["contrast"]["amount"] = text(ID_CONTRAST_EDIT);

    j["effects"]["saturation"]["enabled"] = checked(ID_SAT_CHECK);
    j["effects"]["saturation"]["amount"] = text(ID_SAT_EDIT);

    j["effects"]["invert"]["enabled"] = checked(ID_INVERT_CHECK);
    j["effects"]["grayscale"]["enabled"] = checked(ID_GRAY_CHECK);

    j["effects"]["pixelate"]["enabled"] = checked(ID_PIXELATE_CHECK);
    j["effects"]["pixelate"]["block_size"] = text(ID_PIXELATE_EDIT);

    j["effects"]["glitch"]["enabled"] = checked(ID_GLITCH_CHECK);
    j["effects"]["glitch"]["intensity"] = text(ID_GLITCH_EDIT);

    j["effects"]["edge_detect"]["enabled"] = checked(ID_EDGE_CHECK);

    j["effects"]["chromatic_aberration"]["enabled"] = checked(ID_CHROMA_CHECK);
    j["effects"]["chromatic_aberration"]["amount"] = text(ID_CHROMA_EDIT);
    int chromaMode = (int)SendMessage(GetDlgItem(g_hwnd, ID_CHROMA_MODE), CB_GETCURSEL, 0, 0);
    j["effects"]["chromatic_aberration"]["mode"] = chromaMode == 1 ? "static" : chromaMode == 2 ? "fade" : "off";
    j["effects"]["chromatic_aberration"]["fade_speed"] = sliderVal(ID_CHROMA_SPEED);

    j["effects"]["sharpness"]["enabled"] = checked(ID_SHARP_CHECK);
    j["effects"]["sharpness"]["amount"] = text(ID_SHARP_EDIT);

    j["effects"]["screen_wave"]["enabled"] = checked(ID_WAVE_CHECK);
    j["effects"]["screen_wave"]["intensity"] = text(ID_WAVE_EDIT);
    j["effects"]["screen_wave"]["speed"] = sliderVal(ID_WAVE_SPEED);

    j["effects"]["motion_trail"]["enabled"] = checked(ID_TRAIL_CHECK);
    int trailSel = (int)SendMessage(GetDlgItem(g_hwnd, ID_TRAIL_FRAMES), CB_GETCURSEL, 0, 0);
    int trailFrames[] = {5, 10, 30, 50, 100};
    j["effects"]["motion_trail"]["frames"] = trailFrames[trailSel % 5];
    j["effects"]["motion_trail"]["opacity"] = sliderVal(ID_TRAIL_OPACITY);

    int sel = (int)SendMessage(GetDlgItem(g_hwnd, ID_BLEND_COMBO), CB_GETCURSEL, 0, 0);
    static const char* MODE_NAMES[] = {"normal","additive","xnor","subtract","multiply","screen","difference","overlay","and","or"};
    if (sel >= 0 && sel < BLEND_COUNT) {
        j["effects"]["blend_mode"]["enabled"] = sel > 0;
        j["effects"]["blend_mode"]["mode"] = MODE_NAMES[sel];
    }

    std::ofstream f("config.json");
    if (f.is_open()) { f << j.dump(2); f.close(); }

    // Also update the in-memory shared config for the overlay thread
    AppConfig cfg;
    LoadConfig("config.json", cfg);
    ConfigApply(cfg);
}

static void UpdateEditFromSlider(int editId, int sliderId) {
    LRESULT pos = SendMessage(GetDlgItem(g_hwnd, sliderId), TBM_GETPOS, 0, 0);
    float val = (float)pos / 100.0f;
    wchar_t buf[32];
    swprintf_s(buf, L"%.3f", val);
    SetDlgItemTextW(g_hwnd, editId, buf);
}

static void CreateEffectRow(HWND parent, int y, const wchar_t* label, int checkId, int sliderId, int editId, float def, float rangeMax) {
    int left = 14;
    HWND c = CreateWindowW(L"BUTTON", label, WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        left, y, 105, 20, parent, (HMENU)checkId, nullptr, nullptr);
    SetWindowTheme(c, L" ", L" ");
    SendMessage(c, WM_SETFONT, (WPARAM)g_font, 0);
    if (sliderId) {
        HWND s = CreateWindowW(L"msctls_trackbar32", L"", WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
            left + 110, y, 130, 20, parent, (HMENU)sliderId, nullptr, nullptr);
        SendMessage(s, TBM_SETRANGE, TRUE, MAKELPARAM(0, (int)(rangeMax * 100)));
        SendMessage(s, TBM_SETPOS, TRUE, (int)(def * 100));
    }
    if (editId) {
        HWND e = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_RIGHT,
            left + 245, y, 60, 20, parent, (HMENU)editId, nullptr, nullptr);
        SendMessage(e, WM_SETFONT, (WPARAM)g_font, 0);
        wchar_t buf[32];
        swprintf_s(buf, L"%.3f", def);
        SetDlgItemTextW(parent, editId, buf);
    }
}

static void CreateEffectRowCheck(HWND parent, int y, const wchar_t* label, int checkId) {
    HWND c = CreateWindowW(L"BUTTON", label, WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        14, y, 200, 20, parent, (HMENU)checkId, nullptr, nullptr);
    SetWindowTheme(c, L" ", L" ");
    SendMessage(c, WM_SETFONT, (WPARAM)g_font, 0);
}

static void InitControls() {
    SendMessage(GetDlgItem(g_hwnd, ID_MASTER_CHECK), BM_SETCHECK, BST_CHECKED, 0);

    auto fillCombo = [](int id, const wchar_t** items, int count) {
        HWND c = GetDlgItem(g_hwnd, id);
        for (int i = 0; i < count; i++) SendMessage(c, CB_ADDSTRING, 0, (LPARAM)items[i]);
        SendMessage(c, CB_SETCURSEL, 0, 0);
        SendMessage(c, WM_SETFONT, (WPARAM)g_font, 0);
    };
    fillCombo(ID_BLEND_COMBO, BLEND_MODES, BLEND_COUNT);

    static const wchar_t* CHROMA_MODES[] = {L"Off", L"Static", L"Fade"};
    fillCombo(ID_CHROMA_MODE, CHROMA_MODES, 3);

    static const wchar_t* TRAIL_FRAMES[] = {L"5 frames", L"10 frames", L"30 frames", L"50 frames", L"100 frames"};
    fillCombo(ID_TRAIL_FRAMES, TRAIL_FRAMES, 5);
}

static void ApplyFontToAll() {
    if (!g_hwnd) return;
    EnumChildWindows(g_hwnd, [](HWND child, LPARAM) -> BOOL {
        SendMessage(child, WM_SETFONT, (WPARAM)g_font, 0);
        return TRUE;
    }, 0);
}

static LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_CREATE) {
        g_hwnd = hwnd;
        InitControls();
        ApplyFontToAll();
        RegisterHotKey(hwnd, 2, MOD_CONTROL | MOD_SHIFT | MOD_ALT, 'K');
        return 0;
    }
    if (msg == WM_CLOSE) {
        if (g_overlayRunning) *g_overlayRunning = false;
        DestroyWindow(hwnd);
        return 0;
    }
    if (msg == WM_DESTROY) {
        g_hwnd = nullptr;
        UnregisterHotKey(hwnd, 2);
        PostQuitMessage(0);
        return 0;
    }
    if (msg == WM_HOTKEY && wParam == 2) {
        if (g_overlayRunning) *g_overlayRunning = false;
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
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);
        if (id == ID_STOP_BTN) {
            if (g_overlayRunning) *g_overlayRunning = false;
            return 0;
        }
        if (code == BN_CLICKED || code == EN_CHANGE || code == CBN_SELCHANGE) {
            WriteConfig();
            return 0;
        }
    }
    if (msg == WM_HSCROLL || msg == WM_VSCROLL) {
        HWND tb = (HWND)lParam;
        if (tb) {
            int id = GetDlgCtrlID(tb);
            int eid = 0;
            if (id == ID_HUE_SLIDER) eid = ID_HUE_EDIT;
            else if (id == ID_CONTRAST_SLIDER) eid = ID_CONTRAST_EDIT;
            else if (id == ID_SAT_SLIDER) eid = ID_SAT_EDIT;
            else if (id == ID_PIXELATE_SLIDER) eid = ID_PIXELATE_EDIT;
            else if (id == ID_GLITCH_SLIDER) eid = ID_GLITCH_EDIT;
            else if (id == ID_CHROMA_SLIDER) eid = ID_CHROMA_EDIT;
            else if (id == ID_CHROMA_SPEED) eid = ID_CHROMA_EDIT; // dummy, write config
            else if (id == ID_SHARP_SLIDER) eid = ID_SHARP_EDIT;
            else if (id == ID_WAVE_SLIDER) eid = ID_WAVE_EDIT;
            else if (id == ID_WAVE_SPEED) eid = ID_WAVE_EDIT; // dummy
            else if (id == ID_TRAIL_OPACITY) { WriteConfig(); return 0; }
            if (eid) { UpdateEditFromSlider(eid, id); WriteConfig(); }
        }
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

    WNDCLASSW wc = {};
    wc.lpfnWndProc = SettingsWndProc;
    wc.hInstance = params->hInstance;
    wc.hbrBackground = g_blackBrush;
    wc.lpszClassName = L"DesktopFXSettingsClass";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc);

    int winW = 500, winH = 700;
    HWND hwnd = CreateWindowExW(0, L"DesktopFXSettingsClass", L"Desktop FX",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, winW, winH,
        nullptr, nullptr, params->hInstance, nullptr);
    if (!hwnd) return 1;

    SetWindowTheme(hwnd, L" ", L" ");
    g_hwnd = hwnd;

    int y = 8;
    auto cb = [&](const wchar_t* label, int id, int x, int w) {
        HWND c = CreateWindowW(L"BUTTON", label, WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            x, y, w, 20, hwnd, (HMENU)id, nullptr, nullptr);
        SetWindowTheme(c, L" ", L" "); SendMessage(c, WM_SETFONT, (WPARAM)g_font, 0);
    };
    auto btn = [&](const wchar_t* label, int id, int x, int w) {
        HWND c = CreateWindowW(L"BUTTON", label, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            x, y - 2, w, 24, hwnd, (HMENU)id, nullptr, nullptr);
        SetWindowTheme(c, L" ", L" "); SendMessage(c, WM_SETFONT, (WPARAM)g_font, 0);
    };
    cb(L"Effects Enabled", ID_MASTER_CHECK, 14, 140);
    btn(L"STOP OVERLAY", ID_STOP_BTN, 200, 120);

    y += 28;
    CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ, 10, y, winW - 20, 2, hwnd, nullptr, nullptr, nullptr);

    y += 12;
    CreateEffectRow(hwnd, y, L"Hue (speed)", ID_HUE_CHECK, ID_HUE_SLIDER, ID_HUE_EDIT, 0.67f, 10.0f); y += 24;
    CreateEffectRow(hwnd, y, L"Contrast", ID_CONTRAST_CHECK, ID_CONTRAST_SLIDER, ID_CONTRAST_EDIT, 1.0f, 5.0f); y += 24;
    CreateEffectRow(hwnd, y, L"Saturation", ID_SAT_CHECK, ID_SAT_SLIDER, ID_SAT_EDIT, 1.0f, 5.0f); y += 24;
    CreateEffectRowCheck(hwnd, y, L"Invert", ID_INVERT_CHECK); y += 20;
    CreateEffectRowCheck(hwnd, y, L"Grayscale", ID_GRAY_CHECK); y += 24;
    CreateEffectRow(hwnd, y, L"Pixelate", ID_PIXELATE_CHECK, ID_PIXELATE_SLIDER, ID_PIXELATE_EDIT, 8.0f, 100.0f); y += 24;
    CreateEffectRow(hwnd, y, L"Glitch", ID_GLITCH_CHECK, ID_GLITCH_SLIDER, ID_GLITCH_EDIT, 0.05f, 1.0f); y += 24;
    CreateEffectRowCheck(hwnd, y, L"Edge Detect", ID_EDGE_CHECK); y += 24;

    // Chromatic Aberration — full row with amount slider + mode + fade speed
    CreateEffectRow(hwnd, y, L"Chromatic Ab.", ID_CHROMA_CHECK, ID_CHROMA_SLIDER, ID_CHROMA_EDIT, 0.003f, 0.1f);
    HWND cm = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
        110, y, 70, 100, hwnd, (HMENU)ID_CHROMA_MODE, nullptr, nullptr);
    SendMessage(cm, WM_SETFONT, (WPARAM)g_font, 0);
    for (int i = 0; i < 3; i++) SendMessage(cm, CB_ADDSTRING, 0, (LPARAM)(i==0?L"Off":i==1?L"Static":L"Fade"));
    SendMessage(cm, CB_SETCURSEL, 0, 0);
    HWND css = CreateWindowW(L"msctls_trackbar32", L"", WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
        185, y, 100, 20, hwnd, (HMENU)ID_CHROMA_SPEED, nullptr, nullptr);
    SendMessage(css, TBM_SETRANGE, TRUE, MAKELPARAM(0, 500)); SendMessage(css, TBM_SETPOS, TRUE, 100);
    HWND csl = CreateWindowW(L"STATIC", L"speed", WS_CHILD | WS_VISIBLE,
        290, y, 60, 20, hwnd, nullptr, nullptr, nullptr);
    SendMessage(csl, WM_SETFONT, (WPARAM)g_font, 0);
    y += 24;

    // Sharpness
    CreateEffectRow(hwnd, y, L"Sharpness", ID_SHARP_CHECK, ID_SHARP_SLIDER, ID_SHARP_EDIT, 1.0f, 10.0f); y += 24;

    // Screen Wave — full row with amount + speed
    CreateEffectRow(hwnd, y, L"Screen Wave", ID_WAVE_CHECK, ID_WAVE_SLIDER, ID_WAVE_EDIT, 0.02f, 1.0f);
    HWND wss = CreateWindowW(L"msctls_trackbar32", L"", WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
        185, y, 100, 20, hwnd, (HMENU)ID_WAVE_SPEED, nullptr, nullptr);
    SendMessage(wss, TBM_SETRANGE, TRUE, MAKELPARAM(0, 500)); SendMessage(wss, TBM_SETPOS, TRUE, 50);
    HWND wsl = CreateWindowW(L"STATIC", L"speed", WS_CHILD | WS_VISIBLE,
        290, y, 60, 20, hwnd, nullptr, nullptr, nullptr);
    SendMessage(wsl, WM_SETFONT, (WPARAM)g_font, 0);
    y += 24;

    // Motion Trail
    cb(L"Motion Trail", ID_TRAIL_CHECK, 14, 105);
    HWND tf = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
        120, y, 100, 100, hwnd, (HMENU)ID_TRAIL_FRAMES, nullptr, nullptr);
    SendMessage(tf, WM_SETFONT, (WPARAM)g_font, 0);
    for (int i = 0; i < 5; i++) {
        static const wchar_t* TFL[] = {L"5 frames",L"10 frames",L"30 frames",L"50 frames",L"100 frames"};
        SendMessage(tf, CB_ADDSTRING, 0, (LPARAM)TFL[i]);
    }
    SendMessage(tf, CB_SETCURSEL, 1, 0);
    HWND to = CreateWindowW(L"msctls_trackbar32", L"", WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
        225, y, 100, 20, hwnd, (HMENU)ID_TRAIL_OPACITY, nullptr, nullptr);
    SendMessage(to, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100)); SendMessage(to, TBM_SETPOS, TRUE, 50);
    HWND tol = CreateWindowW(L"STATIC", L"opacity", WS_CHILD | WS_VISIBLE,
        330, y, 60, 20, hwnd, nullptr, nullptr, nullptr);
    SendMessage(tol, WM_SETFONT, (WPARAM)g_font, 0);
    y += 28;

    // Blend mode
    CreateWindowW(L"STATIC", L"Blend Mode:", WS_CHILD | WS_VISIBLE, 14, y, 100, 20, hwnd, nullptr, nullptr, nullptr);
    HWND bm = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
        115, y, 200, 150, hwnd, (HMENU)ID_BLEND_COMBO, nullptr, nullptr);
    SendMessage(bm, WM_SETFONT, (WPARAM)g_font, 0);
    for (int i = 0; i < BLEND_COUNT; i++) SendMessage(bm, CB_ADDSTRING, 0, (LPARAM)BLEND_MODES[i]);
    SendMessage(bm, CB_SETCURSEL, 0, 0);
    y += 24;

    CreateWindowW(L"STATIC", L"Ctrl+Shift+Alt+K = panic kill overlay",
        WS_CHILD | WS_VISIBLE, 14, y, 350, 20, hwnd, nullptr, nullptr, nullptr);

    ShowWindow(hwnd, SW_SHOW);
    ApplyFontToAll();
    // Force redraw so text colors take effect
    RedrawWindow(hwnd, NULL, NULL, RDW_ERASE | RDW_INVALIDATE | RDW_ALLCHILDREN);

    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    g_hwnd = nullptr;
    return 0;
}
