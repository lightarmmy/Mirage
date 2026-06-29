#include "d3d11_renderer.h"
#include <dxgi1_2.h>
#include <cstdio>
#include <fstream>
#include <ShlObj.h>


// Full-screen triangle vertices (NDC coords + UV)
struct Vertex {
    float x, y;  // NDC position
    float u, v;  // UV
};

static const Vertex g_quad_vertices[] = {
    { -1.0f, -1.0f, 0.0f, 1.0f },  // bottom-left NDC  → UV bottom-left
    {  3.0f, -1.0f, 2.0f, 1.0f },  // bottom-right NDC → UV bottom-right
    { -1.0f,  3.0f, 0.0f, -1.0f }, // top-left NDC     → UV top-left
};

// D3D11 globals
static ID3D11Device*           g_device   = nullptr;
static ID3D11DeviceContext*    g_ctx      = nullptr;
static IDXGISwapChain*         g_swapchain = nullptr;
static IDXGIOutputDuplication* g_dupl     = nullptr;
static ID3D11Buffer*           g_vb       = nullptr;

static ID3D11RenderTargetView* g_backbuffer_rtv = nullptr;
static IDXGIOutput*            g_output         = nullptr;
static HWND                    g_hwnd           = nullptr; // for hide-capture-show

// Double-buffered desktop copies (keep GPU async without sync stalls)
static ID3D11Texture2D*          g_desktop_copy[2]     = {nullptr, nullptr};
static ID3D11ShaderResourceView* g_desktop_copy_srv[2] = {nullptr, nullptr};
static int g_copy_write_idx = 0; // which buffer to CopyResource INTO
static int g_copy_read_idx  = 1; // which buffer to render FROM
static bool g_has_first_frame = false;

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
    g_hwnd = hwnd;
    SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE);

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

    // --- Get physical resolution (ignores DPI scaling) ---
    {
        DEVMODEW dm = {};
        dm.dmSize = sizeof(dm);
        if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &dm)) {
            g_width  = (int)dm.dmPelsWidth;
            g_height = (int)dm.dmPelsHeight;
        } else {
            g_width  = GetSystemMetrics(SM_CXSCREEN);
            g_height = GetSystemMetrics(SM_CYSCREEN);
        }
    }

    {
        IDXGIResource* probe = nullptr;
        DXGI_OUTDUPL_FRAME_INFO probe_info;
        if (SUCCEEDED(g_dupl->AcquireNextFrame(250, &probe_info, &probe))) {
            ID3D11Texture2D* probe_tex = nullptr;
            if (SUCCEEDED(probe->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&probe_tex))) {
                D3D11_TEXTURE2D_DESC desc;
                probe_tex->GetDesc(&desc);
                g_width  = (int)desc.Width;
                g_height = (int)desc.Height;
                probe_tex->Release();
            }
            probe->Release();
            g_dupl->ReleaseFrame();
        }
    }

    // Resize window to match actual DD resolution
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, g_width, g_height, SWP_NOACTIVATE | SWP_SHOWWINDOW);

    // --- SwapChain (flip model, matching DD resolution) ---
    DXGI_SWAP_CHAIN_DESC sc_desc = {};
    sc_desc.BufferCount = 2;
    sc_desc.BufferDesc.Width  = g_width;
    sc_desc.BufferDesc.Height = g_height;
    sc_desc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sc_desc.BufferDesc.RefreshRate.Numerator = 0;
    sc_desc.BufferDesc.RefreshRate.Denominator = 0;
    sc_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sc_desc.OutputWindow = hwnd;
    sc_desc.SampleDesc.Count = 1;
    sc_desc.Windowed = TRUE;
    sc_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

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

    // Desktop copy textures are created lazily on first DD frame to match its format
    g_has_first_frame = false;
    g_copy_write_idx = 0;
    g_copy_read_idx = 1;

    if (g_swapchain) {
        g_ctx->OMSetRenderTargets(0, nullptr, nullptr);
        g_swapchain->ResizeBuffers(2, w, h, DXGI_FORMAT_B8G8R8A8_UNORM, 0);

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

    // --- Acquire desktop frame (non-blocking) ---
    IDXGIResource* desktop_resource = nullptr;
    DXGI_OUTDUPL_FRAME_INFO frame_info;
    HRESULT hr = g_dupl->AcquireNextFrame(0, &frame_info, &desktop_resource);

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

    if (SUCCEEDED(hr)) {
        ID3D11Texture2D* src_tex = nullptr;
        hr = desktop_resource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&src_tex);
        desktop_resource->Release();

        if (SUCCEEDED(hr)) {
            // Lazily create/recreate copy textures to match DD frame format exactly
            D3D11_TEXTURE2D_DESC src_desc;
            src_tex->GetDesc(&src_desc);

            auto ensure_copy = [&](int idx) {
                bool need_recreate = !g_desktop_copy[idx];
                if (g_desktop_copy[idx]) {
                    D3D11_TEXTURE2D_DESC cur_desc;
                    g_desktop_copy[idx]->GetDesc(&cur_desc);
                    if (cur_desc.Width != src_desc.Width || cur_desc.Height != src_desc.Height ||
                        cur_desc.Format != src_desc.Format)
                        need_recreate = true;
                }
                if (need_recreate) {
                    if (g_desktop_copy_srv[idx]) { g_desktop_copy_srv[idx]->Release(); g_desktop_copy_srv[idx] = nullptr; }
                    if (g_desktop_copy[idx])     { g_desktop_copy[idx]->Release(); g_desktop_copy[idx] = nullptr; }
                    D3D11_TEXTURE2D_DESC d = src_desc;
                    d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                    d.Usage = D3D11_USAGE_DEFAULT;
                    d.CPUAccessFlags = 0;
                    d.MiscFlags = 0;
                    g_device->CreateTexture2D(&d, nullptr, &g_desktop_copy[idx]);
                    g_device->CreateShaderResourceView(g_desktop_copy[idx], nullptr, &g_desktop_copy_srv[idx]);
                }
            };

            ensure_copy(0);
            ensure_copy(1);

            g_ctx->CopyResource(g_desktop_copy[g_copy_write_idx], src_tex);
            src_tex->Release();

            g_copy_read_idx = g_copy_write_idx;
            g_copy_write_idx = 1 - g_copy_write_idx;
            g_has_first_frame = true;
        }

        g_dupl->ReleaseFrame();
    }

    if (!g_has_first_frame) return;

    // --- Render effects ---
    int N = (int)active_shaders.size();
    if (N == 0) return;
    if (!g_backbuffer_rtv) return;

    ID3D11RenderTargetView* backbuffer_rtv = g_backbuffer_rtv;
    ID3D11ShaderResourceView* src_srv = g_desktop_copy_srv[g_copy_read_idx];
    int dst_idx = 0;

    for (int i = 0; i < N; i++) {
        bool is_last = (i == N - 1);

        if (N == 1) {
            g_ctx->OMSetRenderTargets(1, &backbuffer_rtv, nullptr);
        } else if (is_last) {
            g_ctx->OMSetRenderTargets(1, &backbuffer_rtv, nullptr);
            src_srv = g_temp_srv[1 - dst_idx];
        } else if (i == 0) {
            g_ctx->OMSetRenderTargets(1, &g_temp_rtv[dst_idx], nullptr);
        } else {
            g_ctx->OMSetRenderTargets(1, &g_temp_rtv[dst_idx], nullptr);
            src_srv = g_temp_srv[1 - dst_idx];
        }

        D3D11_VIEWPORT vp = { 0.0f, 0.0f, (float)g_width, (float)g_height, 0.0f, 1.0f };
        g_ctx->RSSetViewports(1, &vp);
        g_ctx->RSSetScissorRects(0, nullptr);

        g_ctx->PSSetShader(active_shaders[i]->ps, nullptr, 0);
        g_ctx->PSSetShaderResources(0, 1, &src_srv);
        g_ctx->PSSetConstantBuffers(0, 1, &active_shaders[i]->cbuffer);
        g_ctx->Draw(3, 0);

        if (N > 1 && i < N - 1) {
            src_srv = g_temp_srv[dst_idx];
            dst_idx = 1 - dst_idx;
        }
    }

    g_swapchain->Present(1, 0);
}

void renderer_shutdown() {
    if (g_backbuffer_rtv) { g_backbuffer_rtv->Release(); g_backbuffer_rtv = nullptr; }
    release_intermediate_texture(g_temp_tex[0], g_temp_rtv[0], g_temp_srv[0]);
    release_intermediate_texture(g_temp_tex[1], g_temp_rtv[1], g_temp_srv[1]);
    for (int i = 0; i < 2; i++) {
        if (g_desktop_copy_srv[i]) { g_desktop_copy_srv[i]->Release(); }
        if (g_desktop_copy[i])     { g_desktop_copy[i]->Release(); }
    }
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

struct EnumCtx { float* rects; int remaining; int sw; int sh; };

static bool is_desktop_or_own_window(HWND hwnd) {
    wchar_t cls[64];
    if (!GetClassNameW(hwnd, cls, 64)) return false;
    return (_wcsicmp(cls, L"Progman") == 0 ||
            _wcsicmp(cls, L"WorkerW") == 0 ||
            _wcsicmp(cls, L"Shell_TrayWnd") == 0 ||
            _wcsicmp(cls, L"MirageOverlay") == 0);
}

static BOOL CALLBACK enum_windows_callback(HWND hwnd, LPARAM lParam) {
    auto* ctx = (EnumCtx*)lParam;
    if (ctx->remaining <= 0) return FALSE;
    if (!IsWindowVisible(hwnd)) return TRUE;
    if (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) return TRUE;
    if (is_desktop_or_own_window(hwnd)) return TRUE;

    RECT r;
    if (GetWindowRect(hwnd, &r)) {
        int w = r.right - r.left;
        int h = r.bottom - r.top;
        if (w < 50 || h < 50) return TRUE;
        // Skip title-less windows anchored at (0,0) covering >40% screen (wallpaper layers)
        if (r.left <= 0 && r.top <= 0 && w >= ctx->sw * 0.4f && h >= ctx->sh * 0.4f) {
            wchar_t title[256];
            if (GetWindowTextW(hwnd, title, 256) == 0) return TRUE;
        }

        float* dst = ctx->rects;
        dst[0] = (float)r.left;
        dst[1] = (float)r.top;
        dst[2] = (float)r.right;
        dst[3] = (float)r.bottom;
        ctx->rects += 4;
        ctx->remaining--;
    }
    return TRUE;
}

int renderer_enumerate_windows(float* rects, int max_count) {
    EnumCtx ctx = { rects, max_count, g_width, g_height };
    EnumWindows(enum_windows_callback, (LPARAM)&ctx);
    return max_count - ctx.remaining;
}

static std::ofstream g_dump_file;

static BOOL CALLBACK dump_windows_callback(HWND hwnd, LPARAM) {
    if (!IsWindowVisible(hwnd)) return TRUE;
    if (is_desktop_or_own_window(hwnd)) return TRUE;

    wchar_t cls[128], title[256];
    GetClassNameW(hwnd, cls, 128);
    GetWindowTextW(hwnd, title, 256);
    RECT r;
    GetWindowRect(hwnd, &r);
    int w = r.right - r.left, h = r.bottom - r.top;
    DWORD pid;
    GetWindowThreadProcessId(hwnd, &pid);

    char cls_narrow[128] = {}, title_narrow[256] = {};
    WideCharToMultiByte(CP_UTF8, 0, cls, -1, cls_narrow, 128, nullptr, nullptr);
    WideCharToMultiByte(CP_UTF8, 0, title, -1, title_narrow, 256, nullptr, nullptr);

    g_dump_file << "Class: [" << cls_narrow << "]  Title: [" << title_narrow
                << "]  Rect: (" << r.left << "," << r.top << "," << r.right << "," << r.bottom
                << ")  Size: " << w << "x" << h << "  PID: " << pid << "\n";
    return TRUE;
}

void renderer_dump_windows() {
    wchar_t desktop[MAX_PATH];
    SHGetFolderPathW(nullptr, CSIDL_DESKTOP, nullptr, 0, desktop);
    std::wstring path = std::wstring(desktop) + L"\\mirage_debug.txt";
    g_dump_file.open(path, std::ios::out | std::ios::trunc);
    if (!g_dump_file.is_open()) return;
    g_dump_file << "=== Mirage Window Dump ===\nScreen: " << g_width << "x" << g_height << "\n\n";
    EnumWindows(dump_windows_callback, 0);
    g_dump_file << "\n=== End ===\n";
    g_dump_file.close();
}

int renderer_height() { return g_height; }
