#include "input_manager.h"
#include "config_manager.h"
#include <bitset>
#include <unordered_map>

static HHOOK g_keyboard_hook = nullptr;
static std::bitset<256> g_key_state;
static std::unordered_map<std::string, UINT> g_name_to_vk;

// Per-effect runtime state
struct EffectState {
    bool active = false;
    bool prev_keys_down = false;
    float start_time = 0.0f; // for oneshot
};
static std::vector<EffectState> g_effect_states;

// Frame timing
static LARGE_INTEGER g_freq;
static LARGE_INTEGER g_last_time;
static float g_time_delta = 0.0f;

// Mouse
static float g_mouse_x = 0.0f;
static float g_mouse_y = 0.0f;

// Reload flag
static bool g_should_reload = false;

static LRESULT CALLBACK low_level_keyboard_proc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        KBDLLHOOKSTRUCT* kb = (KBDLLHOOKSTRUCT*)lParam;
        UINT vk = kb->vkCode;
        bool down = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);

        if (vk < 256) g_key_state[vk] = down;

        // F5 reload
        if (vk == VK_F5 && down) {
            g_should_reload = true;
        }
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

static void init_vk_map() {
    if (!g_name_to_vk.empty()) return;
    g_name_to_vk["ctrl"]  = VK_CONTROL;
    g_name_to_vk["shift"] = VK_SHIFT;
    g_name_to_vk["alt"]   = VK_MENU;
    g_name_to_vk["win"]   = VK_LWIN;
    g_name_to_vk["space"] = VK_SPACE;
    g_name_to_vk["tab"]   = VK_TAB;
    g_name_to_vk["escape"] = VK_ESCAPE;
    g_name_to_vk["enter"]  = VK_RETURN;
    g_name_to_vk["backspace"] = VK_BACK;
    g_name_to_vk["left"]  = VK_LEFT;
    g_name_to_vk["right"] = VK_RIGHT;
    g_name_to_vk["up"]    = VK_UP;
    g_name_to_vk["down"]  = VK_DOWN;
    for (int i = 0; i < 12; i++) {
        g_name_to_vk["f" + std::to_string(i + 1)] = VK_F1 + i;
    }
    for (char c = 'a'; c <= 'z'; c++) {
        g_name_to_vk[std::string(1, c)] = (UINT)(c - 'a' + 'A');
    }
    for (char c = '0'; c <= '9'; c++) {
        g_name_to_vk[std::string(1, c)] = (UINT)(c - '0' + '0');
    }
}

static bool is_key_down(const std::string& key) {
    UINT vk = g_name_to_vk.at(key);
    if (key == "ctrl")  return g_key_state[VK_LCONTROL] || g_key_state[VK_RCONTROL];
    if (key == "shift") return g_key_state[VK_LSHIFT] || g_key_state[VK_RSHIFT];
    if (key == "alt")   return g_key_state[VK_LMENU] || g_key_state[VK_RMENU];
    if (key == "win")   return g_key_state[VK_LWIN] || g_key_state[VK_RWIN];
    return g_key_state[vk];
}

bool input_init() {
    init_vk_map();
    g_keyboard_hook = SetWindowsHookExW(WH_KEYBOARD_LL, low_level_keyboard_proc,
                                         GetModuleHandleW(nullptr), 0);
    if (!g_keyboard_hook) return false;
    QueryPerformanceFrequency(&g_freq);
    QueryPerformanceCounter(&g_last_time);
    return true;
}

void input_shutdown() {
    if (g_keyboard_hook) {
        UnhookWindowsHookEx(g_keyboard_hook);
        g_keyboard_hook = nullptr;
    }
}

void input_update(const std::vector<EffectConfig>& effects) {
    // Update frame timing
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    g_time_delta = (float)(now.QuadPart - g_last_time.QuadPart) / (float)g_freq.QuadPart;
    g_last_time = now;

    // Update mouse
    POINT pt;
    GetCursorPos(&pt);
    g_mouse_x = (float)pt.x;
    g_mouse_y = (float)pt.y;

    // Ensure state vector matches effects
    if (g_effect_states.size() != effects.size()) {
        g_effect_states.resize(effects.size());
    }

    // Update effect state machine
    float current_time = (float)(now.QuadPart) / (float)g_freq.QuadPart;
    for (size_t i = 0; i < effects.size(); i++) {
        auto& state = g_effect_states[i];
        const auto& cfg = effects[i];

        // Check if all hotkey keys are down
        bool all_down = true;
        for (const auto& key : cfg.keys) {
            if (!is_key_down(key)) {
                all_down = false;
                break;
            }
        }

        bool rising = all_down && !state.prev_keys_down;
        state.prev_keys_down = all_down;

        if (cfg.mode == "hold") {
            state.active = all_down;
        } else if (cfg.mode == "toggle" || cfg.mode == "stack") {
            if (rising) state.active = !state.active;
        } else if (cfg.mode == "oneshot") {
            if (rising) {
                state.active = true;
                state.start_time = current_time;
            }
            if (state.active && (current_time - state.start_time) > 2.0f) {
                state.active = false;
            }
        }
    }
}

static std::vector<bool> g_active_cache;

const std::vector<bool>& input_effect_active_states() {
    g_active_cache.resize(g_effect_states.size());
    for (size_t i = 0; i < g_effect_states.size(); i++) {
        g_active_cache[i] = g_effect_states[i].active;
    }
    return g_active_cache;
}

float input_mouse_x()  { return g_mouse_x; }
float input_mouse_y()  { return g_mouse_y; }
float input_time_delta() { return g_time_delta; }
void input_clear_reload_flag() { g_should_reload = false; }
bool input_should_reload() { return g_should_reload; }
