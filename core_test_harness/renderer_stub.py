import time
from datetime import datetime, timezone


class RendererStub:
    def __init__(self, ring, clock, target_fps=30.0):
        self.ring = ring
        self.clock = clock
        self.target_fps = target_fps
        self._rendered = 0
        self._late = 0
        self._skew_sum = 0.0

    def run_for(self, seconds):
        start_wall = time.time()
        end_wall = start_wall + seconds

        while time.time() < end_wall:
            frame = self.ring.pop(timeout=0.1)
            if frame is None:
                continue

            now = datetime.now(timezone.utc)
            station_ts = frame["station_ts"]

            # Wait until it's time to show this frame
            delay = (station_ts - now).total_seconds()
            if delay > 0:
                time.sleep(delay)

            # After sleeping, measure skew
            now = datetime.now(timezone.utc)
            skew = (now - station_ts).total_seconds()

            self._rendered += 1
            self._skew_sum += skew
            if skew > (1.0 / self.target_fps):
                self._late += 1

        print(f"[RendererStub] Rendered {self._rendered} frames")

    def metrics(self):
        avg_skew = self._skew_sum / max(1, self._rendered)
        return {
            "rendered": self._rendered,
            "late": self._late,
            "avg_skew_s": avg_skew,
        }
