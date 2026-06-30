#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <dwmapi.h>
#include <d3d11.h>
#include <string>
#include <vector>

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
static bool                      g_alpha_warmup = false; // 1-frame delay before alpha=255
static bool                      g_had_effects = false;   // previous frame effect state
static bool                      g_has_active_effects = false;
static bool                      g_dump_pressed = false;
static int                       g_shader_failures = 0;

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
    for (auto* s : g_shaders) shader_unload(s);
    g_shaders.clear();
    g_shader_failures = 0;

    ID3D11Device* device = renderer_get_device();
    for (const auto& cfg : g_config.effects) {
        std::string full_path = get_exe_dir() + cfg.shader_path;
        EffectConfig cfg_with_path = cfg;
        cfg_with_path.shader_path = full_path;

        Shader* s = shader_load(device, cfg_with_path);
        if (s) {
            g_shaders.push_back(s);
        } else {
            g_shaders.push_back(nullptr);
            g_shader_failures++;
        }
    }

    g_effects = g_config.effects;
}

static void update_and_render() {
    // Detect screen resolution changes and propagate to renderer
    {
        int sw = GetSystemMetrics(SM_CXSCREEN);
        int sh = GetSystemMetrics(SM_CYSCREEN);
        if (sw != renderer_width() || sh != renderer_height()) {
            renderer_resize(sw, sh);
        }
    }

    // Update input state machine
    input_update(g_effects);

    // Ctrl+Shift+D: dump all visible windows to Desktop/mirage_debug.txt
    if ((GetAsyncKeyState(VK_CONTROL) & 0x8000) &&
        (GetAsyncKeyState(VK_SHIFT) & 0x8000) &&
        (GetAsyncKeyState('D') & 0x8000)) {
        if (!g_dump_pressed) {
            g_dump_pressed = true;
            renderer_dump_windows();
        }
    } else {
        g_dump_pressed = false;
    }

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

    g_has_active_effects = !active_shaders.empty();

    // Alpha-toggle visibility (window always visible, transparent when idle)
    {
        bool has_effects = g_has_active_effects;

        if (has_effects && !g_had_effects) {
            g_alpha_warmup = true;
            renderer_invalidate_frame();
        }
        if (!has_effects) {
            g_alpha_warmup = false;
        }

        BYTE alpha = (has_effects && !g_alpha_warmup) ? 255 : 0;
        SetLayeredWindowAttributes(g_hwnd, 0, alpha, LWA_ALPHA);

        g_had_effects = has_effects;
    }

    // Always update uniforms and render (alpha=0 hides stale content when idle)
    {
        float mx = input_mouse_x();
        float my = input_mouse_y();
        float dt = input_time_delta();

        static float g_win_rects[64 * 4];
        int win_count = renderer_enumerate_windows(g_win_rects, 64);

        static float total_time = 0.0f;
        total_time += dt;

        ID3D11DeviceContext* ctx = renderer_get_context();
        for (size_t i = 0; i < active_shaders.size(); i++) {
            shader_update_cbuffer(ctx, active_shaders[i],
                mx, my, total_time, dt,
                renderer_width(), renderer_height(),
                win_count, g_win_rects);
        }

        bool presented = renderer_render_frame(active_shaders);
        if (g_alpha_warmup && presented) {
            g_alpha_warmup = false; // front buffer is now fresh, safe to show
        }
    }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    // Fallback: older API if the above fails
    SetProcessDPIAware();

    // Load config
    g_config = load_config(get_exe_dir() + "config.json");

    // Create overlay window at screen size, then renderer_init will correct if needed
    int sw, sh;
    get_screen_size(sw, sh);
    g_hwnd = create_overlay_window(hInstance, sw, sh);
    if (!g_hwnd) return -1;

    // Init renderer (probes DD for true resolution, resizes window to match)
    if (!renderer_init(g_hwnd)) {
        destroy_overlay_window(g_hwnd);
        return -1;
    }

    // Resize window to match renderer resolution exactly
    sw = renderer_width();
    sh = renderer_height();
    SetWindowPos(g_hwnd, HWND_TOPMOST, 0, 0, sw, sh, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    DwmFlush(); // Ensure DWM processes the resize before we hide the window

    // Init input
    if (!input_init()) {
        renderer_shutdown();
        destroy_overlay_window(g_hwnd);
        return -1;
    }

    // Load shaders
    load_all_shaders();

    if (g_shader_failures > 0) {
        char msg[256];
        snprintf(msg, sizeof(msg), "%d shader(s) failed to compile.\n\nCheck Desktop\\mirage_shader_errors.txt for details.", g_shader_failures);
        MessageBoxA(nullptr, msg, "Mirage — Shader Errors", MB_ICONWARNING | MB_OK);
    }

    // Window stays visible — alpha=0 (transparent) until first effect activates

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

        // Throttle when idle — no effects to render
        if (!g_has_active_effects) {
            Sleep(16);
        }
    }

    // Shutdown
    input_shutdown();
    for (auto* s : g_shaders) shader_unload(s);
    renderer_shutdown();
    destroy_overlay_window(g_hwnd);

    return 0;
}
