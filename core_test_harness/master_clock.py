import time
from datetime import datetime, timezone, timedelta

class MasterClock:
    def now_utc(self) -> datetime:
        return datetime.now(timezone.utc)

    def sleep(self, seconds: float) -> None:
        # wall-clock sleep for harness; production uses PaceController
        if seconds > 0:
            time.sleep(seconds)

    def pace(self, period_s: float, last_tick: datetime | None = None) -> datetime:
        # simple pacing helper: sleep until next tick based on wall clock
        target = self.now_utc() + timedelta(seconds=period_s) if last_tick is None \
                 else last_tick + timedelta(seconds=period_s)
        delta = (target - self.now_utc()).total_seconds()
        if delta > 0:
            self.sleep(delta)
        return target
