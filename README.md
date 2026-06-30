# Mirage

A lightweight Windows desktop screen shader overlay. Applies real-time GPU shader effects (color inversion, magnification, black hole, warm color, edge glow, window jiggle, and more) to your entire display via a transparent overlay. Configurable entirely through a JSON file — no UI.

- **< 500 KB** executable (static CRT)
- **60+ FPS** on integrated graphics at 1440p
- **Click-through** — overlay is transparent to mouse input
- **Window-aware** — shaders can access the position of every visible window on screen

![demo](demo.gif)

## For Users

### System Requirements

- Windows 10 or 11 (64-bit)
- DirectX 11 GPU (integrated or discrete)

### Quick Start

1. Download `Mirage.zip` from the [Releases](https://github.com/lightarmmy/Mirage/releases) page
2. Extract the zip — you'll get `mirage.exe`, `config.json`, and a `shaders/` folder
3. Run `mirage.exe` (no installation needed)

### Hotkeys

| Shortcut | Effect |
|----------|--------|
| `Ctrl+Shift+I` | Color Inversion (toggle) |
| `Ctrl+Shift+M` | Mouse Magnifier (hold) |
| `Ctrl+Shift+W` | Warm Color Temperature (toggle) |
| `Ctrl+Shift+E` | Window Edge Glow (toggle) |
| `Ctrl+Shift+J` | Window Jiggle (toggle) |
| `Ctrl+Shift+B` | Black Hole (toggle) |
| `F5` | Reload config & shaders from disk |
| `Ctrl+Shift+D` | Dump window list to `Desktop\mirage_debug.txt` |

### Customizing

Edit `config.json` to change hotkeys, add/remove effects, or tweak shader parameters. Press `F5` to reload without restarting.

```json
{
  "version": 1,
  "effects": [
    {
      "id": "invert",
      "name": "Color Inversion",
      "shader": "shaders/invert.hlsl",
      "hotkey": { "keys": ["ctrl", "shift", "i"], "mode": "toggle" }
    }
  ]
}
```

**Hotkey modes:**
- `"hold"` — active while keys are held
- `"toggle"` — press to switch on/off
- `"oneshot"` — press once, auto-deactivates after 2 seconds
- `"stack"` — press to toggle, multiple effects can stack

**Available keys:** `ctrl`, `shift`, `alt`, `win`, `a`–`z`, `0`–`9`, `f1`–`f12`, `space`, `tab`, `escape`, `enter`, `backspace`, `left`, `right`, `up`, `down`

### Shader Parameters

Each effect can have up to 16 float parameters. Example from the black hole effect:

```json
{
  "id": "blackhole",
  "name": "Black Hole",
  "shader": "shaders/blackhole.hlsl",
  "hotkey": { "keys": ["ctrl", "shift", "b"], "mode": "toggle" },
  "params": {
    "hole_radius": 0.03,
    "disk_gain": 2.2,
    "disk_temp": 5500.0,
    "exposure": 1.4
  }
}
```

Parameters are accessible in the shader as `u_param_<name>` (e.g., `u_param_hole_radius`).

### Troubleshooting

- **Effect has no visible result:** Check `Desktop\mirage_shader_errors.txt` for shader compilation errors. Press `F5` to reload after fixing.
- **Screen flickers briefly on activation:** This is the swap chain warming up — one frame (~16ms) delay, normal behavior.
- **Program won't start:** Make sure `config.json` and `shaders/` are in the same folder as `mirage.exe`.

---

## For Shader Creators

Adding a new effect requires only two files: an HLSL pixel shader and a config entry.

### Quick Example: Sepia Tone

**1. Create `shaders/sepia.hlsl`:**

```hlsl
float4 main(PS_INPUT input) : SV_TARGET {
    float4 color = u_scene.Sample(u_sampler, input.uv);
    float gray = dot(color.rgb, float3(0.299, 0.587, 0.114));
    color.rgb = float3(gray + 0.15, gray + 0.07, gray - 0.12);
    return color;
}
```

**2. Add to `config.json`:**

```json
{
  "id": "sepia",
  "name": "Sepia Tone",
  "shader": "shaders/sepia.hlsl",
  "hotkey": { "keys": ["ctrl", "shift", "s"], "mode": "toggle" }
}
```

Press `F5` — done.

### Available Inputs

Mirage automatically prepends a header to every shader. You can use these without declaring them:

| Name | Type | Register | Description |
|------|------|----------|-------------|
| `u_resolution` | `float2` | cbuffer b0 | Screen dimensions in pixels, e.g. (2560, 1440) |
| `u_mouse` | `float2` | cbuffer b0 | Mouse cursor position in screen pixels |
| `u_time` | `float` | cbuffer b0 | Total running time in seconds (monotonically increasing) |
| `u_time_delta` | `float` | cbuffer b0 | Frame delta time in seconds |
| `u_active_window` | `float4` | cbuffer b0 | Foreground window rect: (left, top, right, bottom) in pixels |
| `u_params[4]` | `float4[4]` | cbuffer b0 | Up to 16 user-configurable float params (see below) |
| `u_window_count` | `uint` | cbuffer b0 | Number of visible windows (capped at 64) |
| `u_window_rects[64]` | `float4[64]` | cbuffer b0 | Each visible window's rect: (left, top, right, bottom) in pixels |
| `u_scene` | `Texture2D` | t0 | Live desktop capture |
| `u_sampler` | `SamplerState` | s0 | Linear clamp sampler for `u_scene` |

Your entry point must be: `float4 main(PS_INPUT input) : SV_TARGET` where `PS_INPUT` provides `float4 pos : SV_POSITION` and `float2 uv : TEXCOORD0` (UV ranges from (0,0) top-left to (1,1) bottom-right).

### Parameters

Parameters from `config.json` are automatically converted to `#define` macros. For example, if your config has `"params": { "intensity": 0.5, "speed": 3.0 }`, the shader header will contain:

```hlsl
#define u_param_intensity u_params[0].x
#define u_param_speed     u_params[0].y
```

Use `u_param_intensity` directly in your shader code. Parameters are mapped alphabetically, so you can have up to 16 — they'll occupy `u_params[0]` through `u_params[3]` in alphabetical order.

A common convention: use `0.0` in config.json to mean "use the shader's built-in default value":

```hlsl
float intensity = u_param_intensity > 0.0 ? u_param_intensity : 0.8; // default 0.8
```

### Window-Aware Effects

To make effects that interact with individual windows, use `u_window_rects`:

```hlsl
for (uint i = 0; i < u_window_count; i++) {
    float4 r = u_window_rects[i]; // (left, top, right, bottom) in pixels
    float2 win_min = r.xy / u_resolution;      // convert to UV
    float2 win_max = r.zw / u_resolution;
    if (input.uv.x >= win_min.x && input.uv.x <= win_max.x &&
        input.uv.y >= win_min.y && input.uv.y <= win_max.y) {
        // This pixel is inside window i — apply effect
    }
}
```

See `shaders/edge_glow.hlsl` and `shaders/window_jiggle.hlsl` for complete examples.

### Shader Reference

| File | What it does | Key techniques |
|------|-------------|----------------|
| `invert.hlsl` | Color inversion | Simplest possible shader (one line) |
| `magnifier.hlsl` | Mouse-following zoom | Mouse interaction, smoothstep blending |
| `warm_color.hlsl` | Color temperature | Kelvin-to-RGB conversion matrix |
| `edge_glow.hlsl` | Glowing window borders | Window rect sampling, edge detection |
| `window_jiggle.hlsl` | Wobbling window edges | Per-edge oscillation, exponential falloff |
| `blackhole.hlsl` | Schwarzschild black hole | Geodesic ray tracing, accretion disk, blackbody radiation |

---

## For Developers

### Build from Source

**Requirements:**
- Visual Studio 2022 BuildTools (or full VS 2022) with C++ workload
- Windows 10 SDK (10.0.22621 or later)
- CMake 3.15+

```bash
# From a Visual Studio Developer Command Prompt:
cd Mirage
cmake -B build -G "NMake Makefiles"
cmake --build build --config Release
```

The output is `build\mirage.exe`. The post-build step automatically copies `config.json` and `shaders/` to the build directory.

For convenience, `build.bat` automates this (edit the `vcvars64.bat` path if your VS installation differs).

### Architecture

```
mirage.exe
├── Overlay Window      WS_EX_LAYERED + WS_EX_TRANSPARENT full-screen window
├── Desktop Duplication  IDXGIOutputDuplication captures the desktop as a texture
├── Swap Chain           DXGI_SWAP_EFFECT_FLIP_DISCARD, double-buffered
├── Shader Manager       Compiles .hlsl files at runtime, manages constant buffers
├── Input Manager        Keyboard hook + GetAsyncKeyState hotkey detection
├── Config Manager       JSON config parsing via nlohmann/json
└── D3D11 Renderer       Multi-effect ping-pong compositing, window enumeration
```

**Key design decisions:**

- **Double-buffered desktop copies** — `g_desktop_copy[2]` with ping-pong indices. `CopyResource` runs asynchronously while the GPU reads from the previous copy, avoiding sync stalls.
- **WDA_EXCLUDEFROMCAPTURE** — the overlay window is excluded from Desktop Duplication capture, preventing feedback loops.
- **Color-format-adaptive copy textures** — created lazily to match the DD frame's exact format at runtime, robust against driver variations.
- **Alpha-toggle visibility** — the overlay window is always visible but toggles between alpha=0 (transparent) and alpha=255 (opaque), eliminating swap chain stale-frame flashes on show/hide transitions.
- **Static CRT (`/MT`)** — no runtime DLL dependencies, single self-contained executable.

### Source File Map

| File | Responsibility |
|------|---------------|
| `main.cpp` | WinMain entry point, message loop, module wiring, F5 reload, window dump |
| `d3d11_renderer.h/cpp` | D3D11 device init, Desktop Duplication, swap chain, frame compositing, window enumeration |
| `shader_manager.h/cpp` | HLSL compilation via D3DCompile, constant buffer creation, per-frame cbuffer update |
| `overlay_window.h/cpp` | Transparent layered window creation, DPI handling, WM_DISPLAYCHANGE |
| `input_manager.h/cpp` | WH_KEYBOARD_LL hook, GetAsyncKeyState hotkey detection, 4 trigger modes |
| `config_manager.h/cpp` | JSON → C++ config parsing, built-in fallback defaults |
| `external/json.hpp` | nlohmann/json single-header (v3.11) |

### Constant Buffer Layout

The GPU constant buffer (`MirageUniforms`, 1152 bytes, `#pragma pack 16`) must match the HLSL cbuffer in `shader_manager.cpp:g_ps_header`. Both the C++ struct and the HLSL declaration are in `shader_manager.cpp`.

```
Offset  Size  Field
0       8     resolution[2]
8       8     mouse[2]
16      4     time
20      4     time_delta
24      8     _pad (→ align 32)
32      16    active_window[4]
48      64    params[16]       ← expanded from 4 to 16 for black hole
112     4     window_count
116     12    _pad2 (→ align 128)
128     1024  window_rects[64][4]
Total:  1152
```

To add new uniforms: update both the C++ `MirageUniforms` struct and the `g_ps_header` HLSL string. Verify with `static_assert` that sizes match. Keep 16-byte alignment for all `float4` members.

### Adding a Built-in Effect (Fallback Default)

Edit `build_defaults()` in `config_manager.cpp`. Defaults are used when `config.json` is missing or corrupt.

### Rendering Flow

```
Per frame:
  1. input_update()         — poll hotkeys, update effect state machine
  2. AcquireNextFrame()     — non-blocking (0ms timeout) DD capture
  3. CopyResource()         — async GPU copy to double-buffer
  4. shader_update_cbuffer()— fill constant buffer for each active shader
  5. For each active shader:
     - PSSetShaderResources  — bind desktop SRV at t0
     - PSSetConstantBuffers  — bind per-shader cbuffer at b0
     - Draw(3, 0)            — full-screen triangle
     - Multi-effect? → ping-pong intermediate render targets
  6. Present(1, 0)           — vsync on
```

### Cutting a Release

```bash
# Build Release
cmake --build build --config Release

# Package
mkdir release\Mirage
copy build\mirage.exe release\Mirage\
copy config.json release\Mirage\
xcopy shaders release\Mirage\shaders\ /E
# Zip release\Mirage\ for distribution
```

### License

MIT — see [LICENSE](LICENSE).
