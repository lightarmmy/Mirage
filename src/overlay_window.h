#pragma once
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

HWND create_overlay_window(HINSTANCE hInstance, int width, int height);
void destroy_overlay_window(HWND hwnd);
void get_screen_size(int& width, int& height);
