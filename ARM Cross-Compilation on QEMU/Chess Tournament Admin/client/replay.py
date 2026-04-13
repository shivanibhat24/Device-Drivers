"""
Replay viewer for chess-arm-tournament.
Lets users step through any completed game move by move.
"""
import tkinter as tk
from tkinter import ttk
import chess
import chess.pgn
import io
from typing import Optional


SQ_SIZE  = 60
BOARD_PX = SQ_SIZE * 8
LIGHT_SQ = "#F0D9B5"
DARK_SQ  = "#B58863"
MOVE_SQ  = "#CDD16E"
BG       = "#1E1E2E"
FG       = "#CDD6F4"
ACCENT   = "#89B4FA"
BTN_BG   = "#313244"


class ReplayViewer(tk.Toplevel):
    """
    Standalone Toplevel window.
    Pass a list of UCI move strings (e.g. ["e2e4","e7e5",...])
    or a PGN string.
    """

    def __init__(self, parent, moves: list[str] = None,
                 pgn: str = None,
                 white: str = "White", black: str = "Black"):
        super().__init__(parent)
        self.title("♟ Replay Viewer")
        self.configure(bg=BG)
        self.resizable(False, False)

        self.white_name = white
        self.black_name = black
        self._positions: list[chess.Board] = []
        self._moves_san: list[str]         = []
        self._idx = 0

        self._load(moves=moves, pgn=pgn)
        self._build_ui()
        self._render()

    # ── Load ──────────────────────────────────────────────────────────────────
    def _load(self, moves: list[str] = None, pgn: str = None):
        board = chess.Board()
        self._positions = [board.copy()]
        if pgn:
            game = chess.pgn.read_game(io.StringIO(pgn))
            if game:
                node = game
                while node.variations:
                    node = node.variation(0)
                    self._moves_san.append(node.san())
                    board.push(node.move)
                    self._positions.append(board.copy())
        elif moves:
            for uci in moves:
                try:
                    move = chess.Move.from_uci(uci)
                    self._moves_san.append(board.san(move))
                    board.push(move)
                    self._positions.append(board.copy())
                except Exception:
                    pass

    # ── UI ────────────────────────────────────────────────────────────────────
    def _build_ui(self):
        self.geometry(f"{BOARD_PX + 200}x{BOARD_PX + 40}")

        left = tk.Frame(self, bg=BG)
        left.pack(side="left", padx=6, pady=6)

        self.canvas = tk.Canvas(left, width=BOARD_PX, height=BOARD_PX,
                                bg=BG, highlightthickness=0)
        self.canvas.pack()

        # Nav bar
        nav = tk.Frame(left, bg=BG)
        nav.pack(pady=4)
        btn = dict(bg=BTN_BG, fg=FG, font=("Helvetica", 14),
                   relief="flat", cursor="hand2", padx=10)
        tk.Button(nav, text="⏮", command=self._go_start,  **btn).pack(side="left", padx=2)
        tk.Button(nav, text="◀", command=self._go_prev,   **btn).pack(side="left", padx=2)
        tk.Button(nav, text="▶", command=self._go_next,   **btn).pack(side="left", padx=2)
        tk.Button(nav, text="⏭", command=self._go_end,    **btn).pack(side="left", padx=2)

        self._pos_lbl = tk.Label(nav, text="", bg=BG, fg=FG,
                                 font=("Helvetica", 10))
        self._pos_lbl.pack(side="left", padx=8)

        # Right panel: move list
        right = tk.Frame(self, bg=BG, width=190)
        right.pack(side="right", fill="y", padx=6, pady=6)

        tk.Label(right, text=f"⬜ {self.white_name}",
                 bg=BG, fg=FG, font=("Helvetica", 10, "bold")).pack(anchor="w")
        tk.Label(right, text=f"⬛ {self.black_name}",
                 bg=BG, fg=FG, font=("Helvetica", 10)).pack(anchor="w", pady=(0, 8))

        tk.Label(right, text="Moves", bg=BG, fg=ACCENT,
                 font=("Helvetica", 10, "bold")).pack(anchor="w")

        self._move_list = tk.Text(right, width=20, height=26,
                                  bg=BTN_BG, fg=FG, font=("Courier", 9),
                                  relief="flat", state="disabled",
                                  selectbackground=ACCENT)
        self._move_list.pack()
        self._move_list.tag_configure("highlight", background=ACCENT, foreground=BG)

        # Keyboard bindings
        self.bind("<Left>",  lambda _: self._go_prev())
        self.bind("<Right>", lambda _: self._go_next())
        self.bind("<Home>",  lambda _: self._go_start())
        self.bind("<End>",   lambda _: self._go_end())

    # ── Navigation ────────────────────────────────────────────────────────────
    def _go_prev(self):
        if self._idx > 0:
            self._idx -= 1
            self._render()

    def _go_next(self):
        if self._idx < len(self._positions) - 1:
            self._idx += 1
            self._render()

    def _go_start(self):
        self._idx = 0
        self._render()

    def _go_end(self):
        self._idx = len(self._positions) - 1
        self._render()

    # ── Render ────────────────────────────────────────────────────────────────
    def _render(self):
        board = self._positions[self._idx]
        last_move = board.peek() if board.move_stack else None
        self._draw_board(board, last_move)
        self._update_move_list()
        total = len(self._positions) - 1
        self._pos_lbl.config(text=f"Move {self._idx} / {total}")

    def _draw_board(self, board: chess.Board, last_move: Optional[chess.Move]):
        self.canvas.delete("all")
        last_from = last_move.from_square if last_move else -1
        last_to   = last_move.to_square   if last_move else -1

        for sq in chess.SQUARES:
            file_ = chess.square_file(sq)
            rank  = chess.square_rank(sq)
            x = file_ * SQ_SIZE
            y = (7 - rank) * SQ_SIZE
            is_light = (file_ + rank) % 2 == 1

            if sq in (last_from, last_to):
                color = MOVE_SQ
            else:
                color = LIGHT_SQ if is_light else DARK_SQ

            self.canvas.create_rectangle(x, y, x + SQ_SIZE, y + SQ_SIZE,
                                         fill=color, outline="")
            piece = board.piece_at(sq)
            if piece:
                sym = self._piece_symbol(piece)
                self.canvas.create_text(
                    x + SQ_SIZE // 2, y + SQ_SIZE // 2,
                    text=sym, font=("Segoe UI Emoji", 30),
                    fill="white" if piece.color == chess.WHITE else "#1E1E2E"
                )

        # Labels
        for i in range(8):
            self.canvas.create_text(i * SQ_SIZE + SQ_SIZE - 3, BOARD_PX - 3,
                                    text="abcdefgh"[i], font=("Helvetica", 7),
                                    fill="#888", anchor="se")
            self.canvas.create_text(3, i * SQ_SIZE + 3,
                                    text=str(8 - i), font=("Helvetica", 7),
                                    fill="#888", anchor="nw")

    def _update_move_list(self):
        self._move_list.config(state="normal")
        self._move_list.delete("1.0", "end")
        lines = []
        for i in range(0, len(self._moves_san), 2):
            w = self._moves_san[i]
            b = self._moves_san[i + 1] if i + 1 < len(self._moves_san) else ""
            lines.append(f"{i//2 + 1:>3}. {w:<8}{b}")
        self._move_list.insert("end", "\n".join(lines))

        # Highlight current move
        if self._idx > 0:
            move_num = self._idx - 1
            line_num = move_num // 2 + 1
            col_start = 6 if move_num % 2 == 0 else 14
            col_end   = col_start + 8
            self._move_list.tag_add("highlight",
                                    f"{line_num}.{col_start}",
                                    f"{line_num}.{col_end}")
            self._move_list.see(f"{line_num}.0")
        self._move_list.config(state="disabled")

    @staticmethod
    def _piece_symbol(piece: chess.Piece) -> str:
        SYMS = {
            (chess.KING,   True): "♔", (chess.QUEEN,  True): "♕",
            (chess.ROOK,   True): "♖", (chess.BISHOP, True): "♗",
            (chess.KNIGHT, True): "♘", (chess.PAWN,   True): "♙",
            (chess.KING,  False): "♚", (chess.QUEEN, False): "♛",
            (chess.ROOK,  False): "♜", (chess.BISHOP,False): "♝",
            (chess.KNIGHT,False): "♞", (chess.PAWN,  False): "♟",
        }
        return SYMS.get((piece.piece_type, piece.color), "?")
