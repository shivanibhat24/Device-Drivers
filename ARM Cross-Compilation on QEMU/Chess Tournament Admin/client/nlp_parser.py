"""
NLP move parser for chess-arm-tournament.
Converts Whisper transcriptions like "Knight to F5" → UCI "g1f3".
"""
import re
import chess

# Spoken piece names → chess symbol
PIECE_MAP = {
    "king":   "K", "queen":  "Q", "rook":   "R",
    "bishop": "B", "knight": "N", "pawn":   "P",
    # Common mishearings
    "night":  "N", "horse":  "N",
    "castle": "R", "tower":  "R", "boat": "R",
    "canon":  "R",
}

# Spoken file letters (some accents pronounce these differently)
FILE_ALIASES = {
    "alpha": "a", "bravo": "b", "charlie": "c", "delta": "d",
    "echo":  "e", "foxtrot": "f", "golf": "g", "hotel": "h",
}


def _normalize(text: str) -> str:
    """Lowercase, expand file aliases, strip punctuation."""
    text = text.lower()
    for alias, letter in FILE_ALIASES.items():
        text = text.replace(alias, letter)
    text = re.sub(r"[^a-z0-9 ]", " ", text)
    return text


def parse_voice_to_uci(text: str, board: chess.Board) -> str | None:
    """
    Convert natural language move to UCI.

    Supported patterns:
      "knight to f5"        → piece + destination
      "e2 to e4"            → square to square
      "e4"                  → destination only (unambiguous pawn)
      "castle kingside"     → kingside castling
      "castle queenside"    → queenside castling
      "promote to queen"    → last pawn push promotion (auto-detected)
    """
    text = _normalize(text)
    print(f"[NLP] Input: '{text}'")

    # ── Castling ──────────────────────────────────────────────────────────────
    if "castle" in text or "castl" in text or "o-o" in text:
        kingside  = "king"  in text or "short" in text or text.count("o") == 2
        queenside = "queen" in text or "long"  in text or text.count("o") == 3
        if board.turn == chess.WHITE:
            uci = "e1g1" if (kingside or not queenside) else "e1c1"
        else:
            uci = "e8g8" if (kingside or not queenside) else "e8c8"
        try:
            move = chess.Move.from_uci(uci)
            if move in board.legal_moves:
                return uci
        except Exception:
            pass

    # ── Square-to-square: "e2 to e4" or "e2 e4" ──────────────────────────────
    sq_pat = re.search(r'\b([a-h][1-8])\b.{0,6}\b([a-h][1-8])\b', text)
    if sq_pat:
        uci = sq_pat.group(1) + sq_pat.group(2)
        # Handle promotion
        promo = _detect_promotion(text)
        if promo:
            uci += promo
        try:
            move = chess.Move.from_uci(uci)
            if move in board.legal_moves:
                print(f"[NLP] Square pattern → {uci}")
                return uci
        except Exception:
            pass

    # ── Piece + destination: "knight f5", "bishop to c4" ─────────────────────
    piece_sym = None
    for word, sym in PIECE_MAP.items():
        if re.search(r'\b' + word + r'\b', text):
            piece_sym = sym
            break

    dest_match = re.search(r'\b([a-h][1-8])\b', text)
    if dest_match:
        dest_sq = chess.parse_square(dest_match.group(1))
        candidates = []
        for move in board.legal_moves:
            if move.to_square != dest_sq:
                continue
            piece = board.piece_at(move.from_square)
            if piece is None:
                continue
            if piece_sym and piece.symbol().upper() != piece_sym:
                continue
            candidates.append(move)

        if len(candidates) == 1:
            print(f"[NLP] Piece+dest → {candidates[0].uci()}")
            return candidates[0].uci()
        elif len(candidates) > 1:
            # Ambiguous — return first (GUI should warn)
            print(f"[NLP] Ambiguous, picking first: {candidates[0].uci()}")
            return candidates[0].uci()

    print(f"[NLP] Could not parse: '{text}'")
    return None


def _detect_promotion(text: str) -> str | None:
    """Return promotion character if spoken."""
    if "queen"  in text: return "q"
    if "rook"   in text: return "r"
    if "bishop" in text: return "b"
    if "knight" in text or "night" in text: return "n"
    return None


def uci_to_human(uci: str, board: chess.Board) -> str:
    """e.g. 'g1f3' → 'Knight to f3'"""
    try:
        move  = chess.Move.from_uci(uci)
        piece = board.piece_at(move.from_square)
        name  = chess.piece_name(piece.piece_type).capitalize() if piece else "Piece"
        dest  = chess.square_name(move.to_square)
        return f"{name} to {dest}"
    except Exception:
        return uci
