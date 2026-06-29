#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <d3d11.h>
#include <chrono>
#include <string>
#include <vector>
#include <unordered_map>

#include "config_manager.h"
#include "overlay_window.h"
#include "input_manager.h"
#include "shader_manager.h"
#include "d3d11_renderer.h"

static Config                    g_config;
static HWND                      g_hwnd = nullptr;
static std::vector<Shader*>      g_shaders;
static std::vector<EffectConfig> g_effects;
static bool                      g_running = true;
static bool                      g_overlay_visible = false;

static std::string get_exe_dir() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    int len = WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr);
    std::string s(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, path, -1, &s[0], len, nullptr, nullptr);
    size_t pos = s.find_last_of("\\/");
    return s.substr(0, pos + 1);
}

static void load_all_shaders() {
    // Unload old
    for (auto* s : g_shaders) shader_unload(s);
    g_shaders.clear();

    ID3D11Device* device = renderer_get_device();
    for (const auto& cfg : g_config.effects) {
        std::string full_path = get_exe_dir() + cfg.shader_path;
        EffectConfig cfg_with_path = cfg;
        cfg_with_path.shader_path = full_path;

        Shader* s = shader_load(device, cfg_with_path);
        if (s) {
            g_shaders.push_back(s);
        } else {
            // Mark as failed — push null so indices stay aligned
            g_shaders.push_back(nullptr);
        }
    }

    g_effects = g_config.effects;
}

static void update_and_render() {
    // Update input state machine
    input_update(g_effects);

    // Check for F5 reload
    if (input_should_reload()) {
        input_clear_reload_flag();
        g_config = load_config(get_exe_dir() + "config.json");
        load_all_shaders();
    }

    // Collect active effects (by pointer to EffectConfig + Shader pair)
    const auto& active = input_effect_active_states();
    std::vector<Shader*> active_shaders;
    for (size_t i = 0; i < active.size() && i < g_shaders.size(); i++) {
        if (active[i] && g_shaders[i]) {
            active_shaders.push_back(g_shaders[i]);
        }
    }

    // Manage overlay window visibility
    if (!active_shaders.empty() && !g_overlay_visible) {
        ShowWindow(g_hwnd, SW_SHOW);
        g_overlay_visible = true;
    } else if (active_shaders.empty() && g_overlay_visible) {
        ShowWindow(g_hwnd, SW_HIDE);
        g_overlay_visible = false;
    }

    if (!g_overlay_visible) return;

    // Update shader uniforms
    float mx = input_mouse_x();
    float my = input_mouse_y();
    float dt = input_time_delta();

    static float total_time = 0.0f;
    total_time += dt;

    ID3D11DeviceContext* ctx = renderer_get_context();
    for (size_t i = 0; i < active_shaders.size(); i++) {
        shader_update_cbuffer(ctx, active_shaders[i],
            mx, my, total_time, dt,
            renderer_width(), renderer_height());
    }

    // Render
    renderer_render_frame(active_shaders);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    // Load config
    g_config = load_config(get_exe_dir() + "config.json");

    // Create overlay window
    int sw, sh;
    get_screen_size(sw, sh);
    g_hwnd = create_overlay_window(hInstance, sw, sh);
    if (!g_hwnd) return -1;

    // Init renderer
    if (!renderer_init(g_hwnd)) {
        destroy_overlay_window(g_hwnd);
        return -1;
    }

    // Init input
    input_init();

    // Load shaders
    load_all_shaders();

    // Initially hidden — shown when first effect activates
    ShowWindow(g_hwnd, SW_HIDE);

    // Message loop
    MSG msg = {};
    while (g_running) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                g_running = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        update_and_render();

        // Throttle when idle
        if (!g_overlay_visible) {
            Sleep(16); // ~60fps polling for hotkey detection
        }
    }

    // Shutdown
    input_shutdown();
    for (auto* s : g_shaders) shader_unload(s);
    renderer_shutdown();
    destroy_overlay_window(g_hwnd);

    return 0;
}
