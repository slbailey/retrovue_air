from collections import deque
import threading

class FrameRingBuffer:
    def __init__(self, capacity_frames: int = 120):
        self.capacity = capacity_frames
        self._q = deque()
        self._lock = threading.Lock()
        self._cv = threading.Condition(self._lock)
        self.push_count = 0
        self.drop_count = 0

    def push(self, item) -> bool:
        with self._lock:
            if len(self._q) >= self.capacity:
                self.drop_count += 1
                return False
            self._q.append(item)
            self.push_count += 1
            self._cv.notify()
            return True

    def pop(self, timeout: float | None = None):
        with self._cv:
            if not self._q:
                self._cv.wait(timeout=timeout)
            if not self._q:
                return None
            return self._q.popleft()

    def size(self) -> int:
        with self._lock:
            return len(self._q)
