#include "shader_manager.h"
#include <d3dcompiler.h>
#include <fstream>
#include <sstream>
#include <cstdio>

#pragma pack(push, 16)
struct MirageUniforms {
    float resolution[2];              // offset 0
    float mouse[2];                   // offset 8
    float time;                       // offset 16
    float time_delta;                 // offset 20
    float _pad[2];                    // offset 24 → align to 32
    float active_window[4];           // offset 32
    float params[4];                  // offset 48
    unsigned int window_count;        // offset 64
    float _pad2[3];                   // offset 68 → align to 80
    float window_rects[64][4];        // offset 80  (64 windows × float4)
};
#pragma pack(pop)
static_assert(sizeof(MirageUniforms) == 1104, "CBuffer size mismatch");

// Built-in vertex shader (full-screen pass-through triangle)
static const char* g_vs_source = R"(
struct VS_INPUT  { float2 pos : POSITION; float2 uv : TEXCOORD0; };
struct PS_INPUT  { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };
PS_INPUT main(VS_INPUT input) {
    PS_INPUT o;
    o.pos = float4(input.pos, 0.0, 1.0);
    o.uv  = input.uv;
    return o;
}
)";

// Header prepended to every pixel shader source
static const char* g_ps_header = R"(
cbuffer MirageUniforms : register(b0) {
    float2 u_resolution;
    float2 u_mouse;
    float  u_time;
    float  u_time_delta;
    float4 u_active_window;
    float4 u_params;
    uint   u_window_count;
    float4 u_window_rects[64];
};
Texture2D    u_scene   : register(t0);
SamplerState u_sampler : register(s0);

struct PS_INPUT {
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};
)";

static ID3D11VertexShader* g_cached_vs = nullptr;
static ID3D11InputLayout*  g_cached_layout = nullptr;

ID3D11VertexShader* shader_get_builtin_vs(ID3D11Device* device) {
    if (g_cached_vs) return g_cached_vs;

    ID3DBlob* blob = nullptr;
    ID3DBlob* err  = nullptr;
    HRESULT hr = D3DCompile(g_vs_source, strlen(g_vs_source), "vs", nullptr, nullptr,
                             "main", "vs_5_0", 0, 0, &blob, &err);
    if (FAILED(hr)) {
        if (err) OutputDebugStringA((const char*)err->GetBufferPointer());
        if (err) err->Release();
        return nullptr;
    }

    device->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(),
                                nullptr, &g_cached_vs);

    D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    device->CreateInputLayout(layout, 2, blob->GetBufferPointer(),
                               blob->GetBufferSize(), &g_cached_layout);
    blob->Release();
    return g_cached_vs;
}

ID3D11InputLayout* shader_get_input_layout(ID3D11Device* device) {
    shader_get_builtin_vs(device); // ensures layout is created
    return g_cached_layout;
}

// Generate #define lines for custom params, e.g., "#define u_param_zoom u_params.x\n"
static std::string generate_param_defines(const EffectConfig& cfg) {
    std::string defs;
    int idx = 0;
    const char* swizzle[] = { "x", "y", "z", "w" };
    for (const auto& [name, value] : cfg.params) {
        if (idx >= 4) break;
        char buf[256];
        snprintf(buf, sizeof(buf), "#define u_param_%s u_params.%s\n", name.c_str(), swizzle[idx]);
        defs += buf;
        idx++;
    }
    return defs;
}

Shader* shader_load(ID3D11Device* device, const EffectConfig& cfg) {
    // Read pixel shader source from file
    std::ifstream f(cfg.shader_path);
    if (!f.is_open()) return nullptr;
    std::stringstream ss;
    ss << f.rdbuf();
    std::string ps_source = ss.str();

    // Build full source: header + param defines + user pixel shader code
    std::string full_source = std::string(g_ps_header) + generate_param_defines(cfg) + ps_source;

    // Compile pixel shader
    ID3DBlob* blob = nullptr;
    ID3DBlob* err  = nullptr;
    HRESULT hr = D3DCompile(full_source.c_str(), full_source.size(), cfg.shader_path.c_str(),
                             nullptr, nullptr, "main", "ps_5_0", 0, 0, &blob, &err);
    if (FAILED(hr)) {
        if (err) {
            OutputDebugStringA((const char*)err->GetBufferPointer());
            err->Release();
        }
        return nullptr;
    }

    auto* s = new Shader();
    hr = device->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &s->ps);
    if (FAILED(hr) || !s->ps) {
        blob->Release();
        delete s;
        return nullptr;
    }
    blob->Release();

    // Create constant buffer
    D3D11_BUFFER_DESC cb_desc = {};
    cb_desc.ByteWidth = sizeof(MirageUniforms);
    cb_desc.Usage = D3D11_USAGE_DYNAMIC;
    cb_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cb_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = device->CreateBuffer(&cb_desc, nullptr, &s->cbuffer);
    if (FAILED(hr) || !s->cbuffer) {
        s->ps->Release();
        delete s;
        return nullptr;
    }

    // Store param values
    s->param_count = 0;
    for (const auto& [name, value] : cfg.params) {
        if (s->param_count >= 4) break;
        s->param_values[s->param_count++] = value;
    }

    return s;
}

void shader_unload(Shader* s) {
    if (!s) return;
    if (s->ps) s->ps->Release();
    if (s->cbuffer) s->cbuffer->Release();
    delete s;
}

void shader_update_cbuffer(ID3D11DeviceContext* ctx, Shader* s,
                            float mx, float my, float time, float dt, int w, int h,
                            int win_count, const float* win_rects) {
    MirageUniforms u = {};
    u.resolution[0] = (float)w;
    u.resolution[1] = (float)h;
    u.mouse[0] = mx;
    u.mouse[1] = my;
    u.time = time;
    u.time_delta = dt;

    // Active window rect (best-effort)
    HWND fg = GetForegroundWindow();
    RECT r = {};
    if (fg && GetWindowRect(fg, &r)) {
        u.active_window[0] = (float)r.left;
        u.active_window[1] = (float)r.top;
        u.active_window[2] = (float)r.right;
        u.active_window[3] = (float)r.bottom;
    } else {
        u.active_window[0] = u.active_window[1] =
        u.active_window[2] = u.active_window[3] = -1.0f;
    }

    for (int i = 0; i < s->param_count; i++) {
        u.params[i] = s->param_values[i];
    }

    // Window enumeration data
    u.window_count = (unsigned int)win_count;
    if (win_count > 64) win_count = 64;
    if (win_rects && win_count > 0) {
        memcpy(u.window_rects, win_rects, win_count * 4 * sizeof(float));
    }

    D3D11_MAPPED_SUBRESOURCE mapped;
    ctx->Map(s->cbuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    memcpy(mapped.pData, &u, sizeof(MirageUniforms));
    ctx->Unmap(s->cbuffer, 0);
}
