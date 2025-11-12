from datetime import datetime, timedelta, timezone

class StationTimeMapper:
    def __init__(self, station_start_utc: datetime, source_start_pts_s: float = 0.0, rate: float = 1.0):
        self.station_start_utc = station_start_utc
        self.source_start_pts_s = source_start_pts_s
        self.rate = rate

    def map_pts(self, pts_s: float) -> datetime:
        return self.station_start_utc + timedelta(seconds=(pts_s - self.source_start_pts_s)/self.rate)
