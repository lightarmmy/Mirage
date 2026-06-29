#include "d3d11_renderer.h"
#include <dxgi1_2.h>
#include <cstdio>


// Full-screen triangle vertices (NDC coords + UV)
struct Vertex {
    float x, y;  // NDC position
    float u, v;  // UV
};

static const Vertex g_quad_vertices[] = {
    { -1.0f, -1.0f, 0.0f, 0.0f },
    {  3.0f, -1.0f, 2.0f, 0.0f },
    { -1.0f,  3.0f, 0.0f, 2.0f },
};

// D3D11 globals
static ID3D11Device*           g_device   = nullptr;
static ID3D11DeviceContext*    g_ctx      = nullptr;
static IDXGISwapChain*         g_swapchain = nullptr;
static IDXGIOutputDuplication* g_dupl     = nullptr;
static ID3D11Buffer*           g_vb       = nullptr;

static ID3D11RenderTargetView* g_backbuffer_rtv = nullptr;
static IDXGIOutput*            g_output         = nullptr;

static ID3D11Texture2D*          g_captured_tex  = nullptr;
static ID3D11ShaderResourceView* g_captured_srv  = nullptr;

// Intermediate render targets for multi-effect compositing (ping-pong)
static ID3D11Texture2D*          g_temp_tex[2]   = {nullptr, nullptr};
static ID3D11RenderTargetView*   g_temp_rtv[2]   = {nullptr, nullptr};
static ID3D11ShaderResourceView* g_temp_srv[2]   = {nullptr, nullptr};

static int g_width  = 0;
static int g_height = 0;

// ---- helper: create a texture + RTV + SRV that matches backbuffer ----
static bool create_intermediate_texture(int w, int h,
    ID3D11Texture2D** tex, ID3D11RenderTargetView** rtv, ID3D11ShaderResourceView** srv)
{
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width  = w;
    desc.Height = h;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr = g_device->CreateTexture2D(&desc, nullptr, tex);
    if (FAILED(hr)) return false;

    hr = g_device->CreateRenderTargetView(*tex, nullptr, rtv);
    if (FAILED(hr)) return false;

    hr = g_device->CreateShaderResourceView(*tex, nullptr, srv);
    if (FAILED(hr)) return false;

    return true;
}

static void release_intermediate_texture(
    ID3D11Texture2D*& tex, ID3D11RenderTargetView*& rtv, ID3D11ShaderResourceView*& srv)
{
    if (srv) srv->Release();
    if (rtv) rtv->Release();
    if (tex) tex->Release();
    tex = nullptr; rtv = nullptr; srv = nullptr;
}

// ---- public API ----

bool renderer_init(HWND hwnd) {
    g_width  = GetSystemMetrics(SM_CXSCREEN);
    g_height = GetSystemMetrics(SM_CYSCREEN);

    // --- Find the adapter driving the primary desktop ---
    IDXGIFactory1* factory = nullptr;
    CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory);

    IDXGIAdapter1* chosen_adapter = nullptr;
    IDXGIOutput*   chosen_output  = nullptr;

    IDXGIAdapter1* adapter = nullptr;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; i++) {
        IDXGIOutput* output = nullptr;
        for (UINT j = 0; adapter->EnumOutputs(j, &output) != DXGI_ERROR_NOT_FOUND; j++) {
            DXGI_OUTPUT_DESC desc;
            output->GetDesc(&desc);
            if (desc.AttachedToDesktop) {
                chosen_adapter = adapter;
                chosen_adapter->AddRef();
                chosen_output = output;
                chosen_output->AddRef();
                output->Release();
                break;
            }
            output->Release();
        }
        adapter->Release();
        if (chosen_adapter) break;
    }
    factory->Release();

    if (!chosen_adapter || !chosen_output) return false;

    // --- Create D3D11 device ---
    D3D_FEATURE_LEVEL feature_level;
    UINT flags = 0;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    HRESULT hr = D3D11CreateDevice(
        chosen_adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags,
        nullptr, 0, D3D11_SDK_VERSION,
        &g_device, &feature_level, &g_ctx
    );

    if (FAILED(hr)) {
        // Fallback: use default adapter
        hr = D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
            nullptr, 0, D3D11_SDK_VERSION,
            &g_device, &feature_level, &g_ctx
        );
    }

    if (FAILED(hr)) return false;

    // --- Desktop Duplication ---
    IDXGIOutput1* output1 = nullptr;
    chosen_output->QueryInterface(__uuidof(IDXGIOutput1), (void**)&output1);
    if (output1) {
        output1->DuplicateOutput(g_device, &g_dupl);
        output1->Release();
    }
    g_output = chosen_output;
    chosen_adapter->Release();

    if (!g_dupl) return false;

    // --- SwapChain ---
    DXGI_SWAP_CHAIN_DESC sc_desc = {};
    sc_desc.BufferCount = 1;
    sc_desc.BufferDesc.Width  = g_width;
    sc_desc.BufferDesc.Height = g_height;
    sc_desc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sc_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sc_desc.OutputWindow = hwnd;
    sc_desc.SampleDesc.Count = 1;
    sc_desc.Windowed = TRUE;
    sc_desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    IDXGIFactory1* swapchain_factory = nullptr;
    CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&swapchain_factory);
    hr = swapchain_factory->CreateSwapChain(g_device, &sc_desc, &g_swapchain);
    swapchain_factory->Release();
    if (FAILED(hr)) return false;

    // --- Full-screen triangle vertex buffer ---
    D3D11_BUFFER_DESC vb_desc = {};
    vb_desc.ByteWidth = sizeof(g_quad_vertices);
    vb_desc.Usage = D3D11_USAGE_IMMUTABLE;
    vb_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA vb_data = { g_quad_vertices, 0, 0 };
    hr = g_device->CreateBuffer(&vb_desc, &vb_data, &g_vb);
    if (FAILED(hr)) { OutputDebugStringA("CreateBuffer failed\n"); return false; }

    // --- Render state ---
    UINT stride = sizeof(Vertex);
    UINT offset = 0;
    g_ctx->IASetVertexBuffers(0, 1, &g_vb, &stride, &offset);
    g_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    ID3D11VertexShader* vs = shader_get_builtin_vs(g_device);
    ID3D11InputLayout*  layout = shader_get_input_layout(g_device);
    g_ctx->VSSetShader(vs, nullptr, 0);
    g_ctx->IASetInputLayout(layout);

    D3D11_SAMPLER_DESC smp_desc = {};
    smp_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    smp_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    smp_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    smp_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    ID3D11SamplerState* sampler;
    hr = g_device->CreateSamplerState(&smp_desc, &sampler);
    if (FAILED(hr)) return false;
    g_ctx->PSSetSamplers(0, 1, &sampler);
    sampler->Release(); // ref held by context

    // Disable back-face culling — full-screen triangle reverses winding in screen space
    D3D11_RASTERIZER_DESC rs_desc = {};
    rs_desc.FillMode = D3D11_FILL_SOLID;
    rs_desc.CullMode = D3D11_CULL_NONE;
    ID3D11RasterizerState* rs;
    g_device->CreateRasterizerState(&rs_desc, &rs);
    g_ctx->RSSetState(rs);
    rs->Release();

    // Set viewport to full render target
    D3D11_VIEWPORT vp = { 0.0f, 0.0f, (float)g_width, (float)g_height, 0.0f, 1.0f };
    g_ctx->RSSetViewports(1, &vp);

    // --- Intermediate textures for multi-effect compositing ---
    {
        int w = g_width, h = g_height;
        g_width = 0;
        g_height = 0;
        renderer_resize(w, h);
    }

    return true;
}

void renderer_resize(int w, int h) {
    if (w == g_width && h == g_height) return;
    g_width = w; g_height = h;

    release_intermediate_texture(g_temp_tex[0], g_temp_rtv[0], g_temp_srv[0]);
    release_intermediate_texture(g_temp_tex[1], g_temp_rtv[1], g_temp_srv[1]);

    if (g_swapchain) {
        g_ctx->OMSetRenderTargets(0, nullptr, nullptr);
        g_swapchain->ResizeBuffers(1, w, h, DXGI_FORMAT_B8G8R8A8_UNORM, 0);

        // Recreate backbuffer RTV after resize
        if (g_backbuffer_rtv) { g_backbuffer_rtv->Release(); g_backbuffer_rtv = nullptr; }
        ID3D11Texture2D* bb = nullptr;
        if (SUCCEEDED(g_swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb))) {
            g_device->CreateRenderTargetView(bb, nullptr, &g_backbuffer_rtv);
            bb->Release();
        }
    }

    if (!create_intermediate_texture(w, h, &g_temp_tex[0], &g_temp_rtv[0], &g_temp_srv[0])) return;
    if (!create_intermediate_texture(w, h, &g_temp_tex[1], &g_temp_rtv[1], &g_temp_srv[1])) {
        release_intermediate_texture(g_temp_tex[0], g_temp_rtv[0], g_temp_srv[0]);
        return;
    }

    D3D11_VIEWPORT vp = { 0.0f, 0.0f, (float)w, (float)h, 0.0f, 1.0f };
    g_ctx->RSSetViewports(1, &vp);
}

void renderer_render_frame(const std::vector<Shader*>& active_shaders) {
    if (!g_dupl || !g_ctx) return;

    // --- Acquire desktop frame ---
    IDXGIResource* desktop_resource = nullptr;
    DXGI_OUTDUPL_FRAME_INFO frame_info;
    HRESULT hr = g_dupl->AcquireNextFrame(16, &frame_info, &desktop_resource);

    if (hr == DXGI_ERROR_ACCESS_LOST) {
        g_dupl->Release(); g_dupl = nullptr;
        if (g_output) {
            IDXGIOutput1* output1 = nullptr;
            if (SUCCEEDED(g_output->QueryInterface(__uuidof(IDXGIOutput1), (void**)&output1))) {
                output1->DuplicateOutput(g_device, &g_dupl);
                output1->Release();
            }
        }
        return;
    }
    if (hr == DXGI_ERROR_WAIT_TIMEOUT) return;
    if (FAILED(hr)) return;

    // Get the desktop texture
    if (g_captured_tex) { g_captured_tex->Release(); g_captured_tex = nullptr; }
    if (g_captured_srv) { g_captured_srv->Release(); g_captured_srv = nullptr; }

    hr = desktop_resource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&g_captured_tex);
    desktop_resource->Release();
    if (FAILED(hr)) { g_dupl->ReleaseFrame(); return; }

    D3D11_TEXTURE2D_DESC tex_desc;
    g_captured_tex->GetDesc(&tex_desc);

    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
    srv_desc.Format = tex_desc.Format;
    srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MipLevels = 1;
    hr = g_device->CreateShaderResourceView(g_captured_tex, &srv_desc, &g_captured_srv);
    if (FAILED(hr)) { g_dupl->ReleaseFrame(); return; }

    // --- Multi-effect compositing with ping-pong ---
    int N = (int)active_shaders.size();
    if (N == 0) {
        g_dupl->ReleaseFrame();
        return;
    }

    if (!g_backbuffer_rtv) { g_dupl->ReleaseFrame(); return; }
    ID3D11RenderTargetView* backbuffer_rtv = g_backbuffer_rtv;

    ID3D11ShaderResourceView* src_srv = g_captured_srv;
    int dst_idx = 0;

    for (int i = 0; i < N; i++) {
        bool is_last = (i == N - 1);

        if (N == 1) {
            // Single effect: render directly to backbuffer
            g_ctx->OMSetRenderTargets(1, &backbuffer_rtv, nullptr);
        } else if (is_last) {
            // Last of multiple: render to backbuffer
            g_ctx->OMSetRenderTargets(1, &backbuffer_rtv, nullptr);
            src_srv = g_temp_srv[1 - dst_idx]; // previous pass output
        } else if (i == 0) {
            // First pass: capture -> temp[dst_idx]
            g_ctx->OMSetRenderTargets(1, &g_temp_rtv[dst_idx], nullptr);
        } else {
            // Middle pass: temp[prev] -> temp[dst_idx]
            g_ctx->OMSetRenderTargets(1, &g_temp_rtv[dst_idx], nullptr);
            src_srv = g_temp_srv[1 - dst_idx];
        }

        g_ctx->PSSetShader(active_shaders[i]->ps, nullptr, 0);
        g_ctx->PSSetShaderResources(0, 1, &src_srv);
        g_ctx->PSSetConstantBuffers(0, 1, &active_shaders[i]->cbuffer);
        g_ctx->Draw(3, 0);

        if (N > 1 && i < N - 1) {
            src_srv = g_temp_srv[dst_idx];
            dst_idx = 1 - dst_idx;
        }
    }

    // --- Present ---
    g_swapchain->Present(1, 0); // VSync on

    g_dupl->ReleaseFrame();
}

void renderer_shutdown() {
    if (g_backbuffer_rtv) { g_backbuffer_rtv->Release(); g_backbuffer_rtv = nullptr; }
    release_intermediate_texture(g_temp_tex[0], g_temp_rtv[0], g_temp_srv[0]);
    release_intermediate_texture(g_temp_tex[1], g_temp_rtv[1], g_temp_srv[1]);
    if (g_captured_srv) g_captured_srv->Release();
    if (g_captured_tex) g_captured_tex->Release();
    if (g_vb) g_vb->Release();
    if (g_dupl) g_dupl->Release();
    if (g_output) g_output->Release();
    if (g_swapchain) g_swapchain->Release();
    if (g_ctx) g_ctx->Release();
    if (g_device) g_device->Release();
}

ID3D11Device*        renderer_get_device()    { return g_device; }
ID3D11DeviceContext* renderer_get_context()   { return g_ctx; }
int renderer_width()  { return g_width; }
int renderer_height() { return g_height; }
