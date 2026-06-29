#include "overlay_window.h"

static const wchar_t* WINDOW_CLASS = L"MirageOverlay";

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_DISPLAYCHANGE: {
        int w = (int)(lParam & 0xFFFF);
        int h = (int)(lParam >> 16);
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        return 0;
    }
    case WM_CLOSE:
        return 0; // ignore, we close programmatically only
    case WM_ERASEBKGND:
        return 1; // skip background erase
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void get_screen_size(int& width, int& height) {
    DEVMODEW dm = {};
    dm.dmSize = sizeof(dm);
    if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &dm)) {
        width  = (int)dm.dmPelsWidth;
        height = (int)dm.dmPelsHeight;
    } else {
        width  = GetSystemMetrics(SM_CXSCREEN);
        height = GetSystemMetrics(SM_CYSCREEN);
    }
}

HWND create_overlay_window(HINSTANCE hInstance, int width, int height) {
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc   = wnd_proc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = WINDOW_CLASS;
    wc.hCursor       = nullptr;
    wc.hbrBackground = nullptr; // no background — layered window uses per-pixel alpha

    static bool registered = false;
    if (!registered) {
        RegisterClassExW(&wc);
        registered = true;
    }

    DWORD ex_style = WS_EX_TRANSPARENT | WS_EX_LAYERED | WS_EX_TOPMOST
                   | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
    DWORD style = WS_POPUP;

    HWND hwnd = CreateWindowExW(
        ex_style, WINDOW_CLASS, L"Mirage", style,
        0, 0, width, height,
        nullptr, nullptr, hInstance, nullptr
    );

    SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);

    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    return hwnd;
}

void destroy_overlay_window(HWND hwnd) {
    if (hwnd) DestroyWindow(hwnd);
}
