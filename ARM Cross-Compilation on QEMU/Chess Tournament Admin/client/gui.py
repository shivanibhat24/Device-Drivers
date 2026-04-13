"""
GUI for chess-arm-tournament.
tkinter shell + pygame chessboard embedded inside it.
"""
import tkinter as tk
from tkinter import ttk, messagebox, simpledialog
import threading
import chess
import chess.svg
import json

from tcp_client    import ChessTCPClient
from lichess_auth  import verify_user, format_profile
from nlp_parser    import parse_voice_to_uci, uci_to_human
from voice         import load_model, listen_async
from engine_loader import load_engine_from_github

# ── Colours ───────────────────────────────────────────────────────────────────
LIGHT_SQ  = "#F0D9B5"
DARK_SQ   = "#B58863"
HL_SQ     = "#AAD751"   # selected square
MOVE_SQ   = "#CDD16E"   # last move
BG        = "#1E1E2E"
FG        = "#CDD6F4"
ACCENT    = "#89B4FA"
BTN_BG    = "#313244"
BTN_FG    = "#CDD6F4"

SQ_SIZE   = 70
BOARD_PX  = SQ_SIZE * 8


class ChessGUI:
    def __init__(self, root: tk.Tk):
        self.root      = root
        self.root.title("♟  Chess ARM Tournament")
        self.root.configure(bg=BG)
        self.root.resizable(False, False)

        self.board     = chess.Board()
        self.client    = ChessTCPClient()
        self.my_color  = None   # chess.WHITE / chess.BLACK
        self.selected  = None   # selected square index
        self.last_move = None   # chess.Move
        self.engine    = None   # EnginePlayer
        self.username  = ""
        self.opponent  = ""
        self.mode      = None   # "pvp" | "engine"
        self.listening = False

        self._build_login_screen()

    # ══════════════════════════════════════════════════════════════════════════
    #  SCREENS
    # ══════════════════════════════════════════════════════════════════════════

    def _clear(self):
        for w in self.root.winfo_children():
            w.destroy()

    # ── Login ─────────────────────────────────────────────────────────────────
    def _build_login_screen(self):
        self._clear()
        self.root.geometry("480x320")

        tk.Label(self.root, text="♟  Chess ARM Tournament",
                 font=("Helvetica", 20, "bold"), bg=BG, fg=ACCENT).pack(pady=(30, 4))
        tk.Label(self.root, text="Powered by QEMU · Boost.Asio · Whisper AI",
                 font=("Helvetica", 9), bg=BG, fg="#6C7086").pack()

        frm = tk.Frame(self.root, bg=BG)
        frm.pack(pady=20)

        tk.Label(frm, text="Lichess Username", bg=BG, fg=FG,
                 font=("Helvetica", 11)).grid(row=0, column=0, sticky="w", pady=4)
        self._user_entry = tk.Entry(frm, font=("Helvetica", 12), width=22,
                                    bg=BTN_BG, fg=FG, insertbackground=FG,
                                    relief="flat", bd=4)
        self._user_entry.grid(row=0, column=1, padx=8)

        tk.Label(frm, text="Server Host", bg=BG, fg=FG,
                 font=("Helvetica", 11)).grid(row=1, column=0, sticky="w", pady=4)
        self._host_entry = tk.Entry(frm, font=("Helvetica", 12), width=22,
                                    bg=BTN_BG, fg=FG, insertbackground=FG,
                                    relief="flat", bd=4)
        self._host_entry.insert(0, "127.0.0.1")
        self._host_entry.grid(row=1, column=1, padx=8)

        tk.Label(frm, text="Server Port", bg=BG, fg=FG,
                 font=("Helvetica", 11)).grid(row=2, column=0, sticky="w", pady=4)
        self._port_entry = tk.Entry(frm, font=("Helvetica", 12), width=22,
                                    bg=BTN_BG, fg=FG, insertbackground=FG,
                                    relief="flat", bd=4)
        self._port_entry.insert(0, "5555")
        self._port_entry.grid(row=2, column=1, padx=8)

        self._status_lbl = tk.Label(self.root, text="", bg=BG, fg="#F38BA8",
                                    font=("Helvetica", 10))
        self._status_lbl.pack()

        tk.Button(self.root, text="  Verify & Connect  ",
                  command=self._on_login,
                  bg=ACCENT, fg=BG, font=("Helvetica", 11, "bold"),
                  relief="flat", cursor="hand2", padx=10, pady=6).pack(pady=6)

    def _on_login(self):
        username = self._user_entry.get().strip()
        host     = self._host_entry.get().strip()
        port     = int(self._port_entry.get().strip())

        if not username:
            self._status_lbl.config(text="Please enter a Lichess username")
            return

        self._status_lbl.config(text="Verifying with Lichess…", fg=FG)
        self.root.update()

        profile = verify_user(username)
        if profile is None:
            self._status_lbl.config(text=f"❌ User '{username}' not found on Lichess", fg="#F38BA8")
            return

        self.username = profile["username"]
        self._status_lbl.config(text=f"✓ {format_profile(profile)}", fg="#A6E3A1")
        self.root.update()

        # Connect TCP
        try:
            self.client = ChessTCPClient(host, port)
            self.client.on_message = self._on_server_message
            self.client.connect()
        except Exception as e:
            self._status_lbl.config(text=f"❌ TCP connect failed: {e}", fg="#F38BA8")
            return

        self.client.auth(self.username)
        self._build_mode_screen()

    # ── Mode select ───────────────────────────────────────────────────────────
    def _build_mode_screen(self):
        self._clear()
        self.root.geometry("480x360")

        tk.Label(self.root, text=f"Welcome, {self.username}!",
                 font=("Helvetica", 16, "bold"), bg=BG, fg=ACCENT).pack(pady=(28, 4))
        tk.Label(self.root, text="Choose your opponent",
                 font=("Helvetica", 11), bg=BG, fg=FG).pack(pady=(0, 18))

        btn_cfg = dict(bg=BTN_BG, fg=FG, font=("Helvetica", 12),
                       relief="flat", cursor="hand2", padx=14, pady=10, width=24)

        tk.Button(self.root, text="🧑 Play vs Player",
                  command=self._mode_pvp, **btn_cfg).pack(pady=6)
        tk.Button(self.root, text="🤖 Play vs Engine (GitHub)",
                  command=self._mode_engine, **btn_cfg).pack(pady=6)

        self._mode_status = tk.Label(self.root, text="", bg=BG, fg="#F38BA8",
                                     font=("Helvetica", 10))
        self._mode_status.pack(pady=6)

    def _mode_pvp(self):
        opp = simpledialog.askstring("Opponent", "Enter opponent's Lichess username:",
                                     parent=self.root)
        if not opp:
            return
        profile = verify_user(opp.strip())
        if profile is None:
            self._mode_status.config(text=f"❌ User '{opp}' not found on Lichess")
            return
        self.opponent = profile["username"]
        self.client.set_mode("pvp")
        self._build_game_screen()

    def _mode_engine(self):
        url = simpledialog.askstring(
            "Engine GitHub URL",
            "Enter GitHub repo URL of your UCI engine:\n(e.g. https://github.com/official-stockfish/Stockfish)",
            parent=self.root
        )
        if not url:
            return
        self._mode_status.config(text="Cloning & loading engine…", fg=FG)
        self.root.update()

        def _load():
            eng = load_engine_from_github(url.strip())
            if eng is None:
                self.root.after(0, lambda: self._mode_status.config(
                    text="❌ Could not load engine from that repo", fg="#F38BA8"))
                return
            self.engine = eng
            self.client.set_mode("engine")
            self.root.after(0, self._build_game_screen)

        threading.Thread(target=_load, daemon=True).start()

    # ── Game screen ───────────────────────────────────────────────────────────
    def _build_game_screen(self):
        self._clear()
        self.root.geometry(f"{BOARD_PX + 220}x{BOARD_PX + 60}")

        # Left panel: board canvas
        left = tk.Frame(self.root, bg=BG)
        left.pack(side="left", padx=8, pady=8)

        self.canvas = tk.Canvas(left, width=BOARD_PX, height=BOARD_PX,
                                bg=BG, highlightthickness=0)
        self.canvas.pack()
        self.canvas.bind("<Button-1>", self._on_click)

        # Right panel: info + controls
        right = tk.Frame(self.root, bg=BG, width=210)
        right.pack(side="right", fill="y", padx=8, pady=8)

        tk.Label(right, text="♟ Tournament", font=("Helvetica", 13, "bold"),
                 bg=BG, fg=ACCENT).pack(anchor="w")

        self._info_lbl = tk.Label(right, text="", bg=BG, fg=FG,
                                  font=("Helvetica", 9), wraplength=200, justify="left")
        self._info_lbl.pack(anchor="w", pady=4)

        tk.Label(right, text="Move History", bg=BG, fg=FG,
                 font=("Helvetica", 10, "bold")).pack(anchor="w", pady=(10, 2))

        self._history_box = tk.Text(right, width=22, height=16, bg=BTN_BG, fg=FG,
                                    font=("Courier", 9), relief="flat", state="disabled")
        self._history_box.pack()

        self._turn_lbl = tk.Label(right, text="", bg=BG, fg=ACCENT,
                                  font=("Helvetica", 10, "bold"))
        self._turn_lbl.pack(pady=6)

        # Control buttons
        btn_cfg = dict(bg=BTN_BG, fg=FG, font=("Helvetica", 10),
                       relief="flat", cursor="hand2", pady=5, width=20)

        self._voice_btn = tk.Button(right, text="🎤  Speak Move",
                                    command=self._on_voice, **btn_cfg)
        self._voice_btn.pack(pady=3)

        tk.Button(right, text="🏳  Resign", command=self._on_resign, **btn_cfg).pack(pady=3)

        self._voice_status = tk.Label(right, text="", bg=BG, fg="#FAB387",
                                      font=("Helvetica", 9), wraplength=200)
        self._voice_status.pack(pady=4)

        # Load Whisper in background
        threading.Thread(target=load_model, daemon=True).start()

        self._draw_board()
        self._update_info()

    # ══════════════════════════════════════════════════════════════════════════
    #  BOARD RENDERING
    # ══════════════════════════════════════════════════════════════════════════

    def _sq_to_xy(self, sq: int):
        """Square index → top-left canvas pixel (flips if playing black)."""
        file_ = chess.square_file(sq)
        rank  = chess.square_rank(sq)
        if self.my_color == chess.BLACK:
            file_ = 7 - file_
            rank  = 7 - rank
        x = file_ * SQ_SIZE
        y = (7 - rank) * SQ_SIZE
        return x, y

    def _xy_to_sq(self, x: int, y: int) -> int:
        file_ = x // SQ_SIZE
        rank  = 7 - y // SQ_SIZE
        if self.my_color == chess.BLACK:
            file_ = 7 - file_
            rank  = 7 - rank
        return chess.square(file_, rank)

    def _draw_board(self):
        self.canvas.delete("all")
        last_from = self.last_move.from_square if self.last_move else -1
        last_to   = self.last_move.to_square   if self.last_move else -1

        for sq in chess.SQUARES:
            x, y = self._sq_to_xy(sq)
            file_ = chess.square_file(sq)
            rank  = chess.square_rank(sq)
            is_light = (file_ + rank) % 2 == 1

            # Square colour
            if sq == self.selected:
                color = HL_SQ
            elif sq in (last_from, last_to):
                color = MOVE_SQ
            else:
                color = LIGHT_SQ if is_light else DARK_SQ

            self.canvas.create_rectangle(x, y, x + SQ_SIZE, y + SQ_SIZE,
                                         fill=color, outline="")

            # Piece
            piece = self.board.piece_at(sq)
            if piece:
                symbol = self._piece_symbol(piece)
                self.canvas.create_text(
                    x + SQ_SIZE // 2, y + SQ_SIZE // 2,
                    text=symbol, font=("Segoe UI Emoji", 36), fill="white" if piece.color == chess.WHITE else "#1E1E2E"
                )

        # Rank / file labels
        for i in range(8):
            file_ch = "abcdefgh"[i] if self.my_color != chess.BLACK else "hgfedcba"[i]
            rank_ch = str(i + 1)    if self.my_color != chess.BLACK else str(8 - i)
            self.canvas.create_text(i * SQ_SIZE + SQ_SIZE - 4,
                                    BOARD_PX - 4, text=file_ch,
                                    font=("Helvetica", 8), fill="#888", anchor="se")
            self.canvas.create_text(4, i * SQ_SIZE + 4, text=rank_ch,
                                    font=("Helvetica", 8), fill="#888", anchor="nw")

    @staticmethod
    def _piece_symbol(piece: chess.Piece) -> str:
        SYMBOLS = {
            (chess.KING,   True):  "♔", (chess.QUEEN,  True):  "♕",
            (chess.ROOK,   True):  "♖", (chess.BISHOP, True):  "♗",
            (chess.KNIGHT, True):  "♘", (chess.PAWN,   True):  "♙",
            (chess.KING,   False): "♚", (chess.QUEEN,  False): "♛",
            (chess.ROOK,   False): "♜", (chess.BISHOP, False): "♝",
            (chess.KNIGHT, False): "♞", (chess.PAWN,   False): "♟",
        }
        return SYMBOLS.get((piece.piece_type, piece.color), "?")

    # ══════════════════════════════════════════════════════════════════════════
    #  INTERACTION
    # ══════════════════════════════════════════════════════════════════════════

    def _on_click(self, event):
        if self.board.turn != self.my_color:
            return
        sq = self._xy_to_sq(event.x, event.y)
        if self.selected is None:
            piece = self.board.piece_at(sq)
            if piece and piece.color == self.my_color:
                self.selected = sq
        else:
            uci  = chess.square_name(self.selected) + chess.square_name(sq)
            move = chess.Move.from_uci(uci) if uci else None
            # Handle promotion
            if move and self.board.piece_at(self.selected) and \
               self.board.piece_at(self.selected).piece_type == chess.PAWN and \
               chess.square_rank(sq) in (0, 7):
                uci += "q"
                move = chess.Move.from_uci(uci)
            if move and move in self.board.legal_moves:
                self._submit_move(uci)
            self.selected = None
        self._draw_board()

    def _on_voice(self):
        if self.listening:
            return
        if self.board.turn != self.my_color:
            self._voice_status.config(text="Not your turn!")
            return
        self.listening = True
        self._voice_btn.config(text="🔴  Listening…")
        self._voice_status.config(text="Speak your move now…")

        def _done(text):
            self.listening = False
            self.root.after(0, lambda: self._voice_btn.config(text="🎤  Speak Move"))
            uci = parse_voice_to_uci(text, self.board)
            if uci:
                human = uci_to_human(uci, self.board)
                self.root.after(0, lambda: self._voice_status.config(
                    text=f'Heard: "{text}"\n→ {human}', fg="#A6E3A1"))
                self.root.after(0, lambda: self._submit_move(uci))
            else:
                self.root.after(0, lambda: self._voice_status.config(
                    text=f'Could not parse: "{text}"', fg="#F38BA8"))

        listen_async(_done, duration=4.0)

    def _submit_move(self, uci: str):
        """Validate locally then send to server."""
        try:
            move = chess.Move.from_uci(uci)
        except Exception:
            return
        if move not in self.board.legal_moves:
            self._voice_status.config(text=f"Illegal move: {uci}", fg="#F38BA8")
            return
        self.board.push(move)
        self.last_move = move
        self.client.move(uci)
        self._draw_board()
        self._update_history()
        self._update_info()

        # If engine mode, get engine reply
        if self.engine and self.board.turn != self.my_color:
            threading.Thread(target=self._engine_reply, daemon=True).start()

    def _engine_reply(self):
        uci = self.engine.get_move(self.board)
        self.root.after(0, lambda: self._submit_move(uci))

    def _on_resign(self):
        if messagebox.askyesno("Resign", "Are you sure you want to resign?"):
            self.client.resign()

    # ══════════════════════════════════════════════════════════════════════════
    #  SERVER MESSAGES
    # ══════════════════════════════════════════════════════════════════════════

    def _on_server_message(self, msg: str):
        """Called from background TCP thread — schedule on main thread."""
        self.root.after(0, lambda: self._handle_msg(msg))

    def _handle_msg(self, msg: str):
        if msg.startswith("ASSIGNED:"):
            color = msg.split(":")[1]
            self.my_color = chess.WHITE if color == "white" else chess.BLACK
            self._update_info()

        elif msg.startswith("MOVE:"):
            parts = msg.split(":", 2)
            if len(parts) == 3 and parts[1] != self.username:
                uci = parts[2]
                try:
                    move = chess.Move.from_uci(uci)
                    if move in self.board.legal_moves:
                        self.board.push(move)
                        self.last_move = move
                        self._draw_board()
                        self._update_history()
                        self._update_info()
                except Exception:
                    pass

        elif msg.startswith("STATE:"):
            try:
                data = json.loads(msg[6:])
                self.board = chess.Board(data["fen"])
                self._draw_board()
                self._update_info()
            except Exception:
                pass

        elif msg.startswith("GAMEOVER:"):
            winner = msg.split(":")[1]
            messagebox.showinfo("Game Over",
                f"{'🏆 You won!' if winner[0] == ('w' if self.my_color == chess.WHITE else 'b') else '😢 You lost.'}\nWinner: {winner}")

        elif msg.startswith("JOINED:"):
            self._voice_status.config(
                text=f"{msg.split(':')[1]} joined", fg="#A6E3A1")

        elif msg.startswith("ERROR:"):
            self._voice_status.config(text=msg[6:], fg="#F38BA8")

    # ══════════════════════════════════════════════════════════════════════════
    #  UI HELPERS
    # ══════════════════════════════════════════════════════════════════════════

    def _update_info(self):
        if not hasattr(self, "_info_lbl"):
            return
        color_str = "White ♔" if self.my_color == chess.WHITE else "Black ♚" if self.my_color == chess.BLACK else "?"
        opp_str   = self.opponent or ("Engine 🤖" if self.engine else "Waiting…")
        self._info_lbl.config(
            text=f"You:  {self.username} ({color_str})\nOpp:  {opp_str}\n"
                 f"Mode: {'PvP' if self.mode == 'pvp' else 'Engine' if self.mode == 'engine' else '—'}"
        )
        if hasattr(self, "_turn_lbl"):
            turn = "Your turn ✅" if self.board.turn == self.my_color else "Opponent's turn ⏳"
            if self.board.is_checkmate():
                turn = "Checkmate! 🏁"
            elif self.board.is_stalemate():
                turn = "Stalemate 🤝"
            elif self.board.is_check():
                turn = "CHECK! ⚠️"
            self._turn_lbl.config(text=turn)

    def _update_history(self):
        if not hasattr(self, "_history_box"):
            return
        moves = list(self.board.move_stack)
        lines = []
        for i in range(0, len(moves), 2):
            w = moves[i].uci()
            b = moves[i + 1].uci() if i + 1 < len(moves) else ""
            lines.append(f"{i//2 + 1:>3}. {w:<8}{b}")
        self._history_box.config(state="normal")
        self._history_box.delete("1.0", "end")
        self._history_box.insert("end", "\n".join(lines))
        self._history_box.see("end")
        self._history_box.config(state="disabled")
