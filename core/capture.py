import dxcam


class Capture:
    def __init__(self, monitor=None):
        self._camera = dxcam.create(output_idx=monitor, backend="winrt")
        self._camera.start()
        import time
        time.sleep(0.3)
        frame = self._camera.get_latest_frame()
        self.height, self.width = frame.shape[:2]

    def get_frame(self):
        return self._camera.get_latest_frame()

    def grab(self):
        return self._camera.grab()

    def stop(self):
        try:
            if self._camera.is_capturing:
                self._camera.stop()
            self._camera.release()
        except Exception:
            pass
        self._camera = None
