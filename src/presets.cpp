#include "presets.h"
#include <windows.h>
#include <fstream>
#include <algorithm>

static std::string PresetsDir() {
    char cwd[MAX_PATH];
    GetCurrentDirectoryA(MAX_PATH, cwd);
    return std::string(cwd) + "\\presets";
}

static std::string PresetPath(const std::string& name) {
    return PresetsDir() + "\\" + name + ".json";
}

static std::string ActiveFilePath() {
    return PresetsDir() + "\\.active";
}

static void EnsurePresetsDir() {
    CreateDirectoryA(PresetsDir().c_str(), nullptr);
}

std::vector<std::string> ListPresets() {
    EnsurePresetsDir();
    std::vector<std::string> names;
    WIN32_FIND_DATAA ffd;
    HANDLE hFind = FindFirstFileA((PresetsDir() + "\\*.json").c_str(), &ffd);
    if (hFind == INVALID_HANDLE_VALUE) return names;
    do {
        if (!(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            std::string fn(ffd.cFileName);
            std::string name = fn.substr(0, fn.size() - 5);
            if (!name.empty() && name[0] != '.')
                names.push_back(name);
        }
    } while (FindNextFileA(hFind, &ffd));
    FindClose(hFind);
    std::sort(names.begin(), names.end());
    return names;
}

bool SavePreset(const std::string& name, const json& config) {
    EnsurePresetsDir();
    std::ofstream f(PresetPath(name));
    if (!f.is_open()) return false;
    f << config.dump(2);
    f.close();
    return true;
}

bool LoadPresetFile(const std::string& name, json& config) {
    std::ifstream f(PresetPath(name));
    if (!f.is_open()) return false;
    try {
        f >> config;
        return true;
    } catch (...) {
        return false;
    }
}

bool DeletePreset(const std::string& name) {
    return DeleteFileA(PresetPath(name).c_str()) != FALSE;
}

std::string GetActivePreset() {
    EnsurePresetsDir();
    std::ifstream f(ActiveFilePath());
    if (!f.is_open()) return "";
    std::string name;
    std::getline(f, name);
    return name;
}

void SetActivePreset(const std::string& name) {
    EnsurePresetsDir();
    std::ofstream f(ActiveFilePath());
    if (f.is_open()) {
        f << name;
        f.close();
    }
}
