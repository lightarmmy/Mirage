#pragma once
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <vector>
#include <string>

struct EffectConfig; // forward decl from config_manager.h

bool input_init();
void input_shutdown();
void input_update(const std::vector<EffectConfig>& effects);
const std::vector<bool>& input_effect_active_states();
float input_mouse_x();
float input_mouse_y();
float input_time_delta();
void input_clear_reload_flag();
bool input_should_reload();
