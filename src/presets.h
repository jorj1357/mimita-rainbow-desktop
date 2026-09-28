#pragma once
#include <string>
#include <vector>
#include "../third_party/json.hpp"
using json = nlohmann::json;

std::vector<std::string> ListPresets();
bool SavePreset(const std::string& name, const json& config);
bool LoadPresetFile(const std::string& name, json& config);
bool DeletePreset(const std::string& name);
std::string GetActivePreset();
void SetActivePreset(const std::string& name);
