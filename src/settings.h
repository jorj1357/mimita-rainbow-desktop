#pragma once
#include <windows.h>
#include <atomic>

struct SettingsWindowParams {
    HINSTANCE hInstance;
    std::atomic<bool>* overlayRunning;
};

int ShowSettingsWindow(SettingsWindowParams* params);
void RefreshPresetDropdown();
