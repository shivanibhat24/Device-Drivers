"""
ELO rating utilities for chess-arm-tournament.
Uses the standard Elo formula for local updates.
Lichess API is used to fetch current ratings (read-only — posting results
requires OAuth which is out of scope for this project).
"""
import math
import requests
from lichess_auth import LICHESS_API, TIMEOUT


# ── Elo calculation ───────────────────────────────────────────────────────────

def expected_score(rating_a: int, rating_b: int) -> float:
    """Expected score for player A against player B."""
    return 1.0 / (1.0 + 10 ** ((rating_b - rating_a) / 400))


def new_rating(rating: int, expected: float, actual: float,
               k: int = 32) -> int:
    """
    Compute new Elo rating.
    actual: 1.0 = win, 0.5 = draw, 0.0 = loss
    k: K-factor (32 for most club players, 16 for established players)
    """
    return round(rating + k * (actual - expected))


def compute_rating_change(white_rating: int, black_rating: int,
                          result: str) -> tuple[int, int]:
    """
    Returns (new_white_rating, new_black_rating).
    result: '1-0' | '0-1' | '1/2-1/2'
    """
    ew = expected_score(white_rating, black_rating)
    eb = expected_score(black_rating, white_rating)

    if result == "1-0":
        aw, ab = 1.0, 0.0
    elif result == "0-1":
        aw, ab = 0.0, 1.0
    else:
        aw, ab = 0.5, 0.5

    k = _k_factor(white_rating)
    new_w = new_rating(white_rating, ew, aw, k)
    new_b = new_rating(black_rating, eb, ab, _k_factor(black_rating))
    return new_w, new_b


def _k_factor(rating: int) -> int:
    """FIDE-style K-factor."""
    if rating < 1300: return 40
    if rating < 2000: return 32
    if rating < 2400: return 24
    return 16


def rating_diff_str(old: int, new: int) -> str:
    diff = new - old
    sign = "+" if diff >= 0 else ""
    return f"{new} ({sign}{diff})"


# ── Lichess fetch ─────────────────────────────────────────────────────────────

def fetch_lichess_rating(username: str, variant: str = "blitz") -> int | None:
    """
    Fetch a player's current Lichess rating for a given variant.
    variant: blitz | bullet | rapid | classical | puzzle
    """
    try:
        r = requests.get(f"{LICHESS_API}/user/{username}", timeout=TIMEOUT)
        if r.status_code == 200:
            perfs = r.json().get("perfs", {})
            return perfs.get(variant, {}).get("rating")
    except Exception:
        pass
    return None


def fetch_all_ratings(username: str) -> dict[str, int]:
    """Return dict of variant → rating for a user."""
    try:
        r = requests.get(f"{LICHESS_API}/user/{username}", timeout=TIMEOUT)
        if r.status_code == 200:
            perfs = r.json().get("perfs", {})
            return {v: perfs[v]["rating"] for v in perfs if "rating" in perfs[v]}
    except Exception:
        pass
    return {}
