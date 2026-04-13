"""
Tournament bracket for chess-arm-tournament.
Single-elimination bracket with seeding, BYEs, and result tracking.
"""
import math
import json
from dataclasses import dataclass, field
from typing import Optional


@dataclass
class TournamentMatch:
    match_id:  int
    round_num: int
    white:     Optional[str] = None   # lichess username
    black:     Optional[str] = None
    winner:    Optional[str] = None
    moves:     list[str]     = field(default_factory=list)
    result:    str           = "*"    # PGN result: 1-0 / 0-1 / 1/2-1/2 / *


@dataclass
class TournamentPlayer:
    username: str
    rating:   int  = 1500
    score:    float = 0.0
    wins:     int   = 0
    losses:   int   = 0
    draws:    int   = 0


class Tournament:
    """Single-elimination bracket tournament."""

    def __init__(self, name: str = "ARM Chess Tournament"):
        self.name     = name
        self.players: list[TournamentPlayer] = []
        self.matches: list[TournamentMatch]  = []
        self.current_round = 1
        self.started  = False
        self._match_counter = 0

    # ── Player management ─────────────────────────────────────────────────────
    def add_player(self, username: str, rating: int = 1500):
        if any(p.username == username for p in self.players):
            return
        self.players.append(TournamentPlayer(username=username, rating=rating))

    def remove_player(self, username: str):
        self.players = [p for p in self.players if p.username != username]

    # ── Bracket generation ────────────────────────────────────────────────────
    def start(self):
        """Seed players by rating and generate round 1 pairings."""
        if len(self.players) < 2:
            raise ValueError("Need at least 2 players")
        self.started = True
        # Seed by rating descending
        seeded = sorted(self.players, key=lambda p: p.rating, reverse=True)
        # Pad to power of 2 with BYEs
        size = 2 ** math.ceil(math.log2(len(seeded)))
        while len(seeded) < size:
            seeded.append(TournamentPlayer(username="BYE", rating=0))
        self._generate_round(seeded, round_num=1)

    def _generate_round(self, players: list[TournamentPlayer], round_num: int):
        """Pair players into matches for a given round."""
        for i in range(0, len(players), 2):
            self._match_counter += 1
            w = players[i].username
            b = players[i + 1].username if i + 1 < len(players) else "BYE"
            match = TournamentMatch(
                match_id  = self._match_counter,
                round_num = round_num,
                white     = w,
                black     = b,
            )
            # Auto-advance BYE
            if b == "BYE":
                match.winner = w
                match.result = "1-0"
                self._update_score(w, "win")
            elif w == "BYE":
                match.winner = b
                match.result = "0-1"
                self._update_score(b, "win")
            self.matches.append(match)

    def record_result(self, match_id: int, winner: Optional[str],
                      result: str, moves: list[str] = None):
        """
        Record match result and advance bracket.
        winner=None for draws (both advance in swiss; eliminated in single-elim).
        """
        match = self._get_match(match_id)
        if match is None:
            return False
        match.winner = winner
        match.result = result
        match.moves  = moves or []

        if winner:
            loser = match.black if winner == match.white else match.white
            self._update_score(winner, "win")
            self._update_score(loser,  "loss")
        else:
            self._update_score(match.white, "draw")
            self._update_score(match.black, "draw")

        # Check if round is complete
        if self._round_complete():
            self._advance_bracket()
        return True

    def _round_complete(self) -> bool:
        round_matches = [m for m in self.matches if m.round_num == self.current_round]
        return all(m.winner is not None for m in round_matches)

    def _advance_bracket(self):
        """Collect winners and generate next round."""
        winners_names = [m.winner for m in self.matches
                         if m.round_num == self.current_round and m.winner != "BYE"]
        if len(winners_names) <= 1:
            return  # Tournament over
        self.current_round += 1
        winner_players = [TournamentPlayer(username=w,
                          rating=self._get_player(w).rating if self._get_player(w) else 1500)
                          for w in winners_names]
        self._generate_round(winner_players, self.current_round)

    # ── Queries ───────────────────────────────────────────────────────────────
    def get_bracket(self) -> dict:
        """Return full bracket as dict (for GUI rendering)."""
        rounds = {}
        for m in self.matches:
            rounds.setdefault(m.round_num, []).append({
                "match_id": m.match_id,
                "white":    m.white,
                "black":    m.black,
                "winner":   m.winner,
                "result":   m.result,
            })
        return {
            "name":          self.name,
            "current_round": self.current_round,
            "rounds":        rounds,
            "standings":     self.get_standings(),
        }

    def get_standings(self) -> list[dict]:
        active = [p for p in self.players if p.username != "BYE"]
        active.sort(key=lambda p: (p.score, p.wins), reverse=True)
        return [{"rank": i+1, "username": p.username, "score": p.score,
                 "wins": p.wins, "losses": p.losses, "draws": p.draws}
                for i, p in enumerate(active)]

    def get_current_matches(self) -> list[TournamentMatch]:
        return [m for m in self.matches
                if m.round_num == self.current_round and m.winner is None]

    def champion(self) -> Optional[str]:
        finals = [m for m in self.matches if m.round_num == self.current_round]
        if len(finals) == 1 and finals[0].winner:
            return finals[0].winner
        return None

    def to_json(self) -> str:
        return json.dumps(self.get_bracket(), indent=2)

    # ── Helpers ───────────────────────────────────────────────────────────────
    def _get_match(self, match_id: int) -> Optional[TournamentMatch]:
        return next((m for m in self.matches if m.match_id == match_id), None)

    def _get_player(self, username: str) -> Optional[TournamentPlayer]:
        return next((p for p in self.players if p.username == username), None)

    def _update_score(self, username: str, outcome: str):
        p = self._get_player(username)
        if p is None or username == "BYE":
            return
        if outcome == "win":
            p.score += 1.0; p.wins   += 1
        elif outcome == "loss":
            p.losses += 1
        elif outcome == "draw":
            p.score += 0.5; p.draws  += 1
