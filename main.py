import json
import time

import glfw
import keyboard
import ctypes

from core.capture import Capture
from core.overlay import create_overlay, destroy_overlay
from core.renderer import Renderer
from core.config_watcher import AtomicConfig, start_watcher
from core.debug_log import LOG


def main():
    with open("config.json", "r") as f:
        cfg0 = json.load(f)

    if cfg0.get("debug_log", False):
        LOG.enable()
    LOG.write("=== desktop-fx starting ===")

    atomic_config = AtomicConfig(cfg0)
    observer = start_watcher("config.json", atomic_config)

    capture = Capture()
    w, h = capture.width, capture.height
    LOG.write(f"capture: {w}x{h}")

    window = create_overlay(w, h)
    renderer = Renderer(w, h)

    start_time = time.perf_counter()
    running = True
    shown = False

    panic_flag = [False]
    keyboard.add_hotkey("ctrl+shift+alt+k", lambda: panic_flag.__setitem__(0, True))

    while running and not glfw.window_should_close(window):
        if panic_flag[0]:
            running = False
            break

        cfg = atomic_config.get()

        if cfg.get("enabled", True):
            frame = capture.grab()
            if frame is not None:
                t = time.perf_counter() - start_time
                renderer.upload_frame(frame)
                renderer.render(cfg, t)
                if not shown:
                    glfw.show_window(window)
                    shown = True

        glfw.swap_buffers(window)
        glfw.poll_events()

    LOG.write("shutting down")
    keyboard.unhook_all()
    observer.stop()
    observer.join()
    capture.stop()
    renderer.destroy()
    destroy_overlay(window)
    glfw.terminate()


if __name__ == "__main__":
    try:
        main()
    except Exception as e:
        LOG.write(f"EXCEPTION: {e}")
        import traceback
        LOG.write(traceback.format_exc())
        raise
