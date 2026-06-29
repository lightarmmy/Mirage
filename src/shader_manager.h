#pragma once
#include <d3d11.h>
#include <string>
#include "config_manager.h"

struct Shader {
    ID3D11PixelShader* ps = nullptr;
    ID3D11Buffer* cbuffer = nullptr;
    int param_count = 0;
    float param_values[4] = {0, 0, 0, 0};
};

ID3D11VertexShader* shader_get_builtin_vs(ID3D11Device* device);
ID3D11InputLayout*  shader_get_input_layout(ID3D11Device* device);
Shader* shader_load(ID3D11Device* device, const EffectConfig& cfg);
void    shader_unload(Shader* s);
void    shader_update_cbuffer(ID3D11DeviceContext* ctx, Shader* s,
                              float mx, float my, float time, float dt, int w, int h,
                              int win_count, const float* win_rects);
