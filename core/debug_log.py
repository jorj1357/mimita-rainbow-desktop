import time


class Log:
    def __init__(self):
        self._path = None
        self._enabled = False

    def enable(self, path="debug.log"):
        self._path = path
        self._enabled = True
        with open(self._path, "w") as f:
            f.write(f"=== desktop-fx debug log {time.strftime('%Y-%m-%d %H:%M:%S')} ===\n")
            f.flush()

    def disable(self):
        self._enabled = False

    def write(self, msg):
        if not self._enabled:
            return
        line = f"[{time.perf_counter():.3f}] {msg}\n"
        with open(self._path, "a") as f:
            f.write(line)
            f.flush()


LOG = Log()
