import ctypes
from ctypes import wintypes

import glfw

WS_EX_LAYERED = 0x00080000
WS_EX_TRANSPARENT = 0x00000020
WS_EX_TOPMOST = 0x00000008
WS_EX_TOOLWINDOW = 0x00000080
WS_EX_NOACTIVATE = 0x08000000
GWL_EXSTYLE = -20
WM_NCHITTEST = 0x0084
HTTRANSPARENT = -1

_user32 = ctypes.windll.user32


def create_overlay(width, height, title="desktop-fx"):
    if not glfw.init():
        raise RuntimeError("glfw init failed")

    glfw.window_hint(glfw.DECORATED, False)
    glfw.window_hint(glfw.FLOATING, True)
    glfw.window_hint(glfw.TRANSPARENT_FRAMEBUFFER, True)
    glfw.window_hint(glfw.RESIZABLE, False)
    glfw.window_hint(glfw.VISIBLE, False)

    window = glfw.create_window(width, height, title, None, None)
    if not window:
        glfw.terminate()
        raise RuntimeError("glfw window creation failed")

    glfw.make_context_current(window)
    glfw.swap_interval(0)
    glfw.set_window_pos(window, 0, 0)

    hwnd = glfw.get_win32_window(window)
    style = WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE
    current = _user32.GetWindowLongW(hwnd, GWL_EXSTYLE)
    _user32.SetWindowLongW(hwnd, GWL_EXSTYLE, current | style)

    return window


def destroy_overlay(window):
    glfw.destroy_window(window)
    glfw.terminate()
