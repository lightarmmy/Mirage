#pragma once
#include <string>
#include <vector>
#include <map>

struct HotkeyConfig {
    std::vector<std::string> keys;
    std::string mode; // "hold", "toggle", "oneshot", "stack"
};

struct EffectConfig {
    std::string id;
    std::string name;
    std::string shader_path;
    std::string mode;           // copied from hotkey.mode for convenience
    std::vector<std::string> keys; // copied from hotkey.keys
    std::map<std::string, float> params; // ordered for deterministic shader param mapping
};

struct Config {
    int version = 0;
    std::vector<EffectConfig> effects;
};

Config load_config(const std::string& path);
