#pragma once
#include <windows.h>

HWND CreateOverlayWindow(HINSTANCE hInstance, int width, int height, bool fullscreen);
LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
