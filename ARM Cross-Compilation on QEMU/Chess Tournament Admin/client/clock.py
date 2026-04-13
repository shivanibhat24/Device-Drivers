"""
Chess clock with time controls for chess-arm-tournament.
Supports classical, rapid, blitz, bullet, and custom time controls.
"""
import time
import threading


PRESETS = {
    "Bullet  1+0":    (60,    0),
    "Bullet  2+1":    (120,   1),
    "Blitz   3+0":    (180,   0),
    "Blitz   3+2":    (180,   2),
    "Blitz   5+0":    (300,   0),
    "Rapid  10+0":    (600,   0),
    "Rapid  10+5":    (600,   5),
    "Classical 15+10":(900,  10),
    "Classical 30+0": (1800,  0),
    "Unlimited":      (0,     0),   # 0 = no limit
}


class ChessClock:
    """
    Dual countdown clock.
    white_time / black_time in seconds (float).
    increment added after each move.
    """

    def __init__(self, initial_seconds: float = 300, increment: float = 0):
        self.initial   = initial_seconds
        self.increment = increment
        self.unlimited = (initial_seconds == 0)

        self.white_time = float(initial_seconds)
        self.black_time = float(initial_seconds)

        self._active  = None   # True = white, False = black, None = paused
        self._last_ts = None
        self._lock    = threading.Lock()
        self._timer   = None
        self.on_flag  = None   # callback(color: str) called on timeout

    # ── Control ───────────────────────────────────────────────────────────────
    def start_white(self):
        self._switch(True)

    def start_black(self):
        self._switch(False)

    def press(self, color_is_white: bool):
        """Player just moved — stop their clock, add increment, start opponent's."""
        with self._lock:
            if self.unlimited:
                return
            self._tick()
            if color_is_white:
                self.white_time += self.increment
            else:
                self.black_time += self.increment
        self._switch(not color_is_white)

    def pause(self):
        with self._lock:
            self._tick()
            self._active = None

    def reset(self):
        self.pause()
        self.white_time = float(self.initial)
        self.black_time = float(self.initial)

    # ── Read ──────────────────────────────────────────────────────────────────
    def get_times(self) -> tuple[float, float]:
        """Returns (white_seconds, black_seconds) — live snapshot."""
        with self._lock:
            self._tick()
            return self.white_time, self.black_time

    def format(self, seconds: float) -> str:
        if self.unlimited:
            return "∞"
        if seconds <= 0:
            return "0:00"
        m, s = divmod(int(seconds), 60)
        return f"{m}:{s:02d}"

    # ── Internal ──────────────────────────────────────────────────────────────
    def _switch(self, white_turn: bool):
        with self._lock:
            self._tick()
            self._active  = white_turn
            self._last_ts = time.monotonic()
        self._schedule_check()

    def _tick(self):
        """Subtract elapsed time from active player (must hold lock)."""
        if self._active is None or self._last_ts is None or self.unlimited:
            return
        now     = time.monotonic()
        elapsed = now - self._last_ts
        self._last_ts = now
        if self._active:
            self.white_time = max(0.0, self.white_time - elapsed)
        else:
            self.black_time = max(0.0, self.black_time - elapsed)

    def _schedule_check(self):
        if self._timer:
            self._timer.cancel()
        if self.unlimited or self._active is None:
            return
        with self._lock:
            remaining = self.white_time if self._active else self.black_time
        if remaining <= 0:
            return
        self._timer = threading.Timer(min(remaining, 0.25), self._check_flag)
        self._timer.daemon = True
        self._timer.start()

    def _check_flag(self):
        w, b = self.get_times()
        if self._active is True  and w <= 0 and self.on_flag:
            self.on_flag("white")
        elif self._active is False and b <= 0 and self.on_flag:
            self.on_flag("black")
        else:
            self._schedule_check()
