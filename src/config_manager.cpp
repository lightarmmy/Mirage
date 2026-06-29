#include "config_manager.h"
#include <fstream>
#include <json.hpp>

using json = nlohmann::json;

static Config build_defaults() {
    Config c;
    c.version = 1;
    c.effects = {
        {
            "invert", "Color Inversion", "shaders/invert.hlsl", "toggle",
            {"ctrl", "shift", "i"}, {}
        },
        {
            "magnifier", "Mouse Magnifier", "shaders/magnifier.hlsl", "hold",
            {"ctrl", "shift", "m"}, {{"zoom", 2.0f}, {"radius", 200.0f}}
        },
        {
            "warm_color", "Warm Color Temp", "shaders/warm_color.hlsl", "toggle",
            {"ctrl", "shift", "w"}, {{"temperature", 6500.0f}}
        },
    };
    return c;
}

Config load_config(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return build_defaults();

    try {
        json j = json::parse(f);
        Config c;
        c.version = j.value("version", 1);

        for (const auto& ej : j.at("effects")) {
            EffectConfig ec;
            ec.id = ej.at("id").get<std::string>();
            ec.name = ej.value("name", ec.id);
            ec.shader_path = ej.at("shader").get<std::string>();

            const auto& hk = ej.at("hotkey");
            ec.keys = hk.at("keys").get<std::vector<std::string>>();
            ec.mode = hk.at("mode").get<std::string>();

            if (ej.contains("params")) {
                for (const auto& [k, v] : ej.at("params").items()) {
                    ec.params[k] = v.get<float>();
                }
            }
            c.effects.push_back(ec);
        }
        return c;
    } catch (...) {
        return build_defaults();
    }
}
