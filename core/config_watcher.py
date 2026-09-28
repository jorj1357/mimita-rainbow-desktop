import json
import os
import threading

from watchdog.observers import Observer
from watchdog.events import FileSystemEventHandler


class AtomicConfig:
    def __init__(self, initial):
        self._lock = threading.Lock()
        self._data = initial

    def get(self):
        with self._lock:
            return self._data.copy()

    def set(self, data):
        with self._lock:
            self._data = data


class _ConfigHandler(FileSystemEventHandler):
    def __init__(self, atomic_config, path):
        self._config = atomic_config
        self._path = path

    def on_modified(self, event):
        if event.src_path == self._path:
            try:
                with open(self._path, "r") as f:
                    data = json.load(f)
                self._config.set(data)
            except Exception:
                pass


def start_watcher(path, atomic_config):
    abs_path = os.path.abspath(path)
    handler = _ConfigHandler(atomic_config, abs_path)
    observer = Observer()
    observer.schedule(handler, os.path.dirname(abs_path), recursive=False)
    observer.start()
    return observer
