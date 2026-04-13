"""
Lichess API integration for chess-arm-tournament.
Verifies usernames and fetches player info.
"""
import requests

LICHESS_API = "https://lichess.org/api"
TIMEOUT     = 6  # seconds


def verify_user(username: str) -> dict | None:
    """
    Check if a Lichess username exists.
    Returns a dict with profile info, or None if not found.
    """
    try:
        r = requests.get(f"{LICHESS_API}/user/{username}", timeout=TIMEOUT)
        if r.status_code == 200:
            d = r.json()
            return {
                "username": d["username"],
                "title":    d.get("title", ""),
                "rating_blitz":   d.get("perfs", {}).get("blitz",  {}).get("rating", "?"),
                "rating_bullet":  d.get("perfs", {}).get("bullet", {}).get("rating", "?"),
                "rating_rapid":   d.get("perfs", {}).get("rapid",  {}).get("rating", "?"),
                "online":   d.get("online", False),
                "patron":   d.get("patron", False),
            }
        if r.status_code == 404:
            return None
    except requests.RequestException as e:
        print(f"[Lichess] API error: {e}")
    return None


def format_profile(profile: dict) -> str:
    """Return a short human-readable profile string."""
    title  = f"{profile['title']} " if profile["title"] else ""
    online = "🟢" if profile["online"] else "🔴"
    return (
        f"{online} {title}{profile['username']}  "
        f"| Blitz: {profile['rating_blitz']}  "
        f"| Bullet: {profile['rating_bullet']}  "
        f"| Rapid: {profile['rating_rapid']}"
    )


def get_daily_puzzle() -> dict | None:
    """Fetch today's Lichess puzzle (bonus feature)."""
    try:
        r = requests.get(f"{LICHESS_API}/puzzle/daily", timeout=TIMEOUT)
        if r.status_code == 200:
            return r.json()
    except Exception:
        pass
    return None
