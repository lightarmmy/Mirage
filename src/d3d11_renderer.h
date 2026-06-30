#pragma once
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <d3d11.h>
#include <vector>
#include "shader_manager.h"

bool renderer_init(HWND hwnd);
void renderer_resize(int w, int h);
bool renderer_render_frame(const std::vector<Shader*>& active_shaders); // returns true if Present was called
void renderer_shutdown();

ID3D11Device*        renderer_get_device();
ID3D11DeviceContext* renderer_get_context();
int renderer_width();
int renderer_height();
int renderer_enumerate_windows(float* rects, int max_count); // fills (l,t,r,b) quads
void renderer_dump_windows(); // write window info to Desktop/mirage_debug.txt
void renderer_invalidate_frame(); // drop stale cached frame when overlay shown
