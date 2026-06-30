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
            {"ctrl", "shift", "w"}, {{"temperature", 3000.0f}}
        },
        {
            "edge_glow", "Window Edge Glow", "shaders/edge_glow.hlsl", "toggle",
            {"ctrl", "shift", "e"}, {{"width", 10.0f}, {"intensity", 0.5f}}
        },
        {
            "window_jiggle", "Window Jiggle", "shaders/window_jiggle.hlsl", "toggle",
            {"ctrl", "shift", "j"}, {{"amount", 6.0f}, {"speed", 4.0f}}
        },
        {
            "blackhole", "Black Hole", "shaders/blackhole.hlsl", "toggle",
            {"ctrl", "shift", "b"},
            {{"hole_radius", 0.03f}, {"disk_gain", 2.2f}, {"disk_temp", 5500.0f},
             {"exposure", 1.4f}, {"disk_speed", 5.0f}, {"star_gain", 0.3f},
             {"disk_incl", 1.5f}, {"disk_inner", 0.0f}, {"disk_outer", 0.0f},
             {"disk_opacity", 0.0f}, {"doppler_mix", 0.6f}, {"disk_beam", 0.0f},
             {"disk_contrast", 0.0f}, {"disk_wind", 0.0f}, {"disk_roll", 0.0f}}
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
