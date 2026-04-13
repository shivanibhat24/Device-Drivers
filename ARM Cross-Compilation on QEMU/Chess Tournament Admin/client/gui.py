"""
GUI for chess-arm-tournament.
Features:
  - tkinter chessboard with click-to-move
  - Move suggestions (legal moves on hover)
  - Chess clock display
  - Spectator mode
  - Tournament bracket window
  - Replay viewer
  - ELO rating display & update
  - Whisper AI voice commands
"""
import tkinter as tk
from tkinter import ttk, messagebox, simpledialog
import threading
import chess
import json

from tcp_client    import ChessTCPClient
from lichess_auth  import verify_user, format_profile
from nlp_parser    import parse_voice_to_uci, uci_to_human
from voice         import load_model, listen_async
from engine_loader import load_engine_from_github
from clock         import ChessClock, PRESETS
from tournament    import Tournament
from replay        import ReplayViewer
from elo           import compute_rating_change, rating_diff_str

# ── Palette ───────────────────────────────────────────────────────────────────
LIGHT_SQ = "#F0D9B5"
DARK_SQ  = "#B58863"
HL_SQ    = "#AAD751"
MOVE_SQ  = "#CDD16E"
HINT_SQ  = "#82C0E8"
BG       = "#1E1E2E"
FG       = "#CDD6F4"
ACCENT   = "#89B4FA"
BTN_BG   = "#313244"
DANGER   = "#F38BA8"
SUCCESS  = "#A6E3A1"
WARNING  = "#FAB387"

SQ_SIZE  = 70
BOARD_PX = SQ_SIZE * 8


class ChessGUI:
    def __init__(self, root: tk.Tk):
        self.root       = root
        self.root.title("♟  Chess ARM Tournament")
        self.root.configure(bg=BG)
        self.root.resizable(False, False)

        self.board      = chess.Board()
        self.client     = ChessTCPClient()
        self.my_color   = None
        self.selected   = None
        self.last_move  = None
        self.hovered_sq = None
        self.hints: set[int] = set()
        self.engine     = None
        self.username   = ""
        self.opponent   = ""
        self.mode       = None
        self.listening  = False
        self.spectating = False
        self.clock      = ChessClock(300, 0)
        self.tournament: Tournament | None = None
        self._move_uci_log: list[str] = []
        self._white_rating = 1500
        self._black_rating = 1500

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
        self.root.geometry("500x430")

        tk.Label(self.root, text="♟  Chess ARM Tournament",
                 font=("Helvetica", 20, "bold"), bg=BG, fg=ACCENT).pack(pady=(28, 4))
        tk.Label(self.root, text="QEMU · Boost.Asio · Whisper AI · Lichess",
                 font=("Helvetica", 9), bg=BG, fg="#6C7086").pack()

        frm = tk.Frame(self.root, bg=BG)
        frm.pack(pady=16)

        def row(label, default=""):
            tk.Label(frm, text=label, bg=BG, fg=FG,
                     font=("Helvetica", 11)).grid(row=row.n, column=0, sticky="w", pady=4)
            e = tk.Entry(frm, font=("Helvetica", 12), width=22,
                         bg=BTN_BG, fg=FG, insertbackground=FG, relief="flat", bd=4)
            e.insert(0, default)
            e.grid(row=row.n, column=1, padx=8)
            row.n += 1
            return e
        row.n = 0

        self._user_entry = row("Lichess Username")
        self._host_entry = row("Server Host", "127.0.0.1")
        self._port_entry = row("Server Port",  "5555")

        tk.Label(frm, text="Time Control", bg=BG, fg=FG,
                 font=("Helvetica", 11)).grid(row=row.n, column=0, sticky="w", pady=4)
        self._clock_var = tk.StringVar(value="Blitz   5+0")
        ttk.Combobox(frm, textvariable=self._clock_var, values=list(PRESETS.keys()),
                     width=20, state="readonly").grid(row=row.n, column=1, padx=8)
        row.n += 1

        self._status_lbl = tk.Label(self.root, text="", bg=BG, fg=DANGER,
                                    font=("Helvetica", 10))
        self._status_lbl.pack()

        tk.Button(self.root, text="  Verify & Connect  ", command=self._on_login,
                  bg=ACCENT, fg=BG, font=("Helvetica", 11, "bold"),
                  relief="flat", cursor="hand2", padx=10, pady=6).pack(pady=6)
        tk.Button(self.root, text="👁  Join as Spectator", command=self._on_spectate,
                  bg=BTN_BG, fg=FG, font=("Helvetica", 10),
                  relief="flat", cursor="hand2", padx=8, pady=4).pack()

    def _on_login(self):
        username = self._user_entry.get().strip()
        host     = self._host_entry.get().strip()
        port     = int(self._port_entry.get().strip())
        secs, inc = PRESETS.get(self._clock_var.get(), (300, 0))

        if not username:
            self._status_lbl.config(text="Please enter a username"); return

        self._status_lbl.config(text="Verifying…", fg=FG); self.root.update()
        profile = verify_user(username)
        if profile is None:
            self._status_lbl.config(text=f"❌ '{username}' not found on Lichess", fg=DANGER)
            return

        self.username       = profile["username"]
        self._white_rating  = profile.get("rating_blitz", 1500) or 1500
        self._status_lbl.config(text=f"✓ {format_profile(profile)}", fg=SUCCESS)
        self.root.update()

        self.clock = ChessClock(secs, inc)
        self.clock.on_flag = self._on_flag
        try:
            self.client = ChessTCPClient(host, port)
            self.client.on_message = self._on_server_message
            self.client.connect()
        except Exception as e:
            self._status_lbl.config(text=f"❌ TCP: {e}", fg=DANGER); return

        self.client.auth(self.username)
        self._build_mode_screen()

    def _on_spectate(self):
        host = self._host_entry.get().strip()
        port = int(self._port_entry.get().strip())
        self.spectating = True
        self.username   = "Spectator"
        try:
            self.client = ChessTCPClient(host, port)
            self.client.on_message = self._on_server_message
            self.client.connect()
        except Exception as e:
            self._status_lbl.config(text=f"❌ TCP: {e}", fg=DANGER); return
        self.client.request_status()
        self._build_game_screen()

    # ── Mode ──────────────────────────────────────────────────────────────────
    def _build_mode_screen(self):
        self._clear()
        self.root.geometry("500x400")
        tk.Label(self.root, text=f"Welcome, {self.username}! ♟",
                 font=("Helvetica", 16, "bold"), bg=BG, fg=ACCENT).pack(pady=(28, 4))
        tk.Label(self.root, text="Choose your opponent",
                 font=("Helvetica", 11), bg=BG, fg=FG).pack(pady=(0, 16))

        b = dict(bg=BTN_BG, fg=FG, font=("Helvetica", 12),
                 relief="flat", cursor="hand2", padx=14, pady=10, width=28)
        tk.Button(self.root, text="🧑  Play vs Player",         command=self._mode_pvp,        **b).pack(pady=5)
        tk.Button(self.root, text="🤖  Play vs Engine (GitHub)",command=self._mode_engine,     **b).pack(pady=5)
        tk.Button(self.root, text="🏆  Start Tournament",       command=self._mode_tournament, **b).pack(pady=5)

        self._mode_status = tk.Label(self.root, text="", bg=BG, fg=DANGER,
                                     font=("Helvetica", 10), wraplength=460)
        self._mode_status.pack(pady=6)

    def _mode_pvp(self):
        opp = simpledialog.askstring("Opponent", "Opponent's Lichess username:", parent=self.root)
        if not opp: return
        profile = verify_user(opp.strip())
        if profile is None:
            self._mode_status.config(text=f"❌ '{opp}' not found"); return
        self.opponent      = profile["username"]
        self._black_rating = profile.get("rating_blitz", 1500) or 1500
        self.mode = "pvp"; self.client.set_mode("pvp"); self._build_game_screen()

    def _mode_engine(self):
        url = simpledialog.askstring("Engine URL",
            "GitHub repo of UCI engine:\n(e.g. https://github.com/official-stockfish/Stockfish)",
            parent=self.root)
        if not url: return
        self._mode_status.config(text="Cloning & building…", fg=FG); self.root.update()
        def _load():
            eng = load_engine_from_github(url.strip())
            if eng is None:
                self.root.after(0, lambda: self._mode_status.config(text="❌ Engine load failed", fg=DANGER))
                return
            self.engine = eng; self.opponent = "Engine 🤖"
            self.mode = "engine"; self.client.set_mode("engine")
            self.root.after(0, self._build_game_screen)
        threading.Thread(target=_load, daemon=True).start()

    def _mode_tournament(self):
        self.tournament = Tournament(f"{self.username}'s Tournament")
        self.tournament.add_player(self.username, self._white_rating)
        self._show_tournament_setup()

    def _show_tournament_setup(self):
        win = tk.Toplevel(self.root)
        win.title("Tournament Setup"); win.configure(bg=BG); win.geometry("360x420")
        tk.Label(win, text="🏆 Tournament Setup", font=("Helvetica", 14, "bold"),
                 bg=BG, fg=ACCENT).pack(pady=12)
        lb = tk.Listbox(win, bg=BTN_BG, fg=FG, font=("Helvetica", 10),
                        width=30, height=10, relief="flat")
        lb.pack(pady=4)
        def refresh():
            lb.delete(0, "end")
            for p in self.tournament.players:
                lb.insert("end", f"  {p.username}  ({p.rating})")
        refresh()
        def add():
            name = simpledialog.askstring("Add Player", "Lichess username:", parent=win)
            if not name: return
            pr = verify_user(name.strip())
            if pr:
                self.tournament.add_player(pr["username"], pr.get("rating_blitz",1500) or 1500)
                refresh()
            else:
                messagebox.showerror("Error", f"'{name}' not found", parent=win)
        def start():
            if len(self.tournament.players) < 2:
                messagebox.showerror("Error", "Need at least 2 players", parent=win); return
            self.tournament.start(); win.destroy(); self._show_bracket()
        tk.Button(win, text="➕  Add Player", command=add,
                  bg=BTN_BG, fg=FG, relief="flat", font=("Helvetica", 10),
                  cursor="hand2", pady=6, width=20).pack(pady=4)
        tk.Button(win, text="🚀  Start Tournament", command=start,
                  bg=ACCENT, fg=BG, relief="flat", font=("Helvetica", 11, "bold"),
                  cursor="hand2", pady=6, width=20).pack(pady=4)

    # ── Game screen ───────────────────────────────────────────────────────────
    def _build_game_screen(self):
        self._clear()
        self.root.geometry(f"{BOARD_PX + 230}x{BOARD_PX + 80}")

        left = tk.Frame(self.root, bg=BG)
        left.pack(side="left", padx=8, pady=8)

        self._black_clock_lbl = tk.Label(left, text="⬛  5:00",
                                          font=("Helvetica", 14, "bold"),
                                          bg=BTN_BG, fg=FG, padx=10, pady=4)
        self._black_clock_lbl.pack(fill="x")

        self.canvas = tk.Canvas(left, width=BOARD_PX, height=BOARD_PX,
                                bg=BG, highlightthickness=0)
        self.canvas.pack()
        self.canvas.bind("<Button-1>", self._on_click)
        self.canvas.bind("<Motion>",   self._on_hover)
        self.canvas.bind("<Leave>",    self._on_leave)

        self._white_clock_lbl = tk.Label(left, text="⬜  5:00",
                                          font=("Helvetica", 14, "bold"),
                                          bg=BTN_BG, fg=FG, padx=10, pady=4)
        self._white_clock_lbl.pack(fill="x")

        right = tk.Frame(self.root, bg=BG, width=220)
        right.pack(side="right", fill="y", padx=8, pady=8)

        tk.Label(right, text="♟ Tournament", font=("Helvetica", 13, "bold"),
                 bg=BG, fg=ACCENT).pack(anchor="w")
        self._info_lbl = tk.Label(right, text="", bg=BG, fg=FG,
                                  font=("Helvetica", 9), wraplength=210, justify="left")
        self._info_lbl.pack(anchor="w", pady=4)
        self._elo_lbl = tk.Label(right, text="", bg=BG, fg=WARNING,
                                 font=("Helvetica", 9), wraplength=210)
        self._elo_lbl.pack(anchor="w")

        tk.Label(right, text="Move History", bg=BG, fg=FG,
                 font=("Helvetica", 10, "bold")).pack(anchor="w", pady=(8, 2))
        self._history_box = tk.Text(right, width=24, height=12, bg=BTN_BG, fg=FG,
                                    font=("Courier", 9), relief="flat", state="disabled")
        self._history_box.pack()

        self._turn_lbl = tk.Label(right, text="", bg=BG, fg=ACCENT,
                                  font=("Helvetica", 10, "bold"))
        self._turn_lbl.pack(pady=4)

        b = dict(bg=BTN_BG, fg=FG, font=("Helvetica", 10),
                 relief="flat", cursor="hand2", pady=4, width=22)

        if not self.spectating:
            self._voice_btn = tk.Button(right, text="🎤  Speak Move",
                                        command=self._on_voice, **b)
            self._voice_btn.pack(pady=2)
            tk.Button(right, text="🏳  Resign", command=self._on_resign, **b).pack(pady=2)

        tk.Button(right, text="📼  Replay Game", command=self._open_replay, **b).pack(pady=2)
        if self.tournament:
            tk.Button(right, text="🏆  Bracket", command=self._show_bracket, **b).pack(pady=2)

        self._voice_status = tk.Label(right, text="", bg=BG, fg=WARNING,
                                      font=("Helvetica", 9), wraplength=210)
        self._voice_status.pack(pady=4)

        if self.spectating:
            tk.Label(right, text="👁  SPECTATING", bg=BG, fg=ACCENT,
                     font=("Helvetica", 11, "bold")).pack(pady=8)

        threading.Thread(target=load_model, daemon=True).start()
        self._draw_board()
        self._update_info()
        self._tick_clock()

    # ══════════════════════════════════════════════════════════════════════════
    #  CLOCK
    # ══════════════════════════════════════════════════════════════════════════

    def _tick_clock(self):
        if not hasattr(self, "_white_clock_lbl"):
            return
        w, b = self.clock.get_times()
        wfg = DANGER if (not self.clock.unlimited and w < 10) else FG
        bfg = DANGER if (not self.clock.unlimited and b < 10) else FG
        self._white_clock_lbl.config(text=f"⬜  {self.clock.format(w)}", fg=wfg)
        self._black_clock_lbl.config(text=f"⬛  {self.clock.format(b)}", fg=bfg)
        act_w = self.board.turn == chess.WHITE
        self._white_clock_lbl.config(bg=ACCENT if act_w else BTN_BG)
        self._black_clock_lbl.config(bg=BTN_BG if act_w else ACCENT)
        self.root.after(250, self._tick_clock)

    def _on_flag(self, color: str):
        winner = "black" if color == "white" else "white"
        self.root.after(0, lambda: messagebox.showinfo(
            "Flag!", f"{color.capitalize()} flagged!\n{winner.capitalize()} wins on time."))

    # ══════════════════════════════════════════════════════════════════════════
    #  BOARD DRAWING
    # ══════════════════════════════════════════════════════════════════════════

    def _sq_to_xy(self, sq):
        f, r = chess.square_file(sq), chess.square_rank(sq)
        if self.my_color == chess.BLACK: f, r = 7-f, 7-r
        return f * SQ_SIZE, (7 - r) * SQ_SIZE

    def _xy_to_sq(self, x, y):
        f = max(0, min(7, x // SQ_SIZE))
        r = max(0, min(7, 7 - y // SQ_SIZE))
        if self.my_color == chess.BLACK: f, r = 7-f, 7-r
        return chess.square(f, r)

    def _draw_board(self):
        self.canvas.delete("all")
        lf = self.last_move.from_square if self.last_move else -1
        lt = self.last_move.to_square   if self.last_move else -1

        for sq in chess.SQUARES:
            x, y = self._sq_to_xy(sq)
            f, r = chess.square_file(sq), chess.square_rank(sq)
            light = (f + r) % 2 == 1
            color = (HL_SQ if sq == self.selected else
                     MOVE_SQ if sq in (lf, lt) else
                     LIGHT_SQ if light else DARK_SQ)
            self.canvas.create_rectangle(x, y, x+SQ_SIZE, y+SQ_SIZE, fill=color, outline="")

            if sq in self.hints:
                cx, cy = x + SQ_SIZE//2, y + SQ_SIZE//2
                r_ = 10 if not self.board.piece_at(sq) else SQ_SIZE//2 - 3
                self.canvas.create_oval(cx-r_, cy-r_, cx+r_, cy+r_,
                                        fill=HINT_SQ, outline="")

            piece = self.board.piece_at(sq)
            if piece:
                self.canvas.create_text(x+SQ_SIZE//2, y+SQ_SIZE//2,
                    text=self._sym(piece), font=("Segoe UI Emoji", 36),
                    fill="white" if piece.color == chess.WHITE else "#1E1E2E")

        for i in range(8):
            f = "abcdefgh"[i] if self.my_color != chess.BLACK else "hgfedcba"[i]
            r = str(i+1)      if self.my_color != chess.BLACK else str(8-i)
            self.canvas.create_text(i*SQ_SIZE+SQ_SIZE-4, BOARD_PX-4,
                                    text=f, font=("Helvetica", 8), fill="#888", anchor="se")
            self.canvas.create_text(4, i*SQ_SIZE+4,
                                    text=r, font=("Helvetica", 8), fill="#888", anchor="nw")

    @staticmethod
    def _sym(piece):
        S = {(chess.KING,True):"♔",(chess.QUEEN,True):"♕",(chess.ROOK,True):"♖",
             (chess.BISHOP,True):"♗",(chess.KNIGHT,True):"♘",(chess.PAWN,True):"♙",
             (chess.KING,False):"♚",(chess.QUEEN,False):"♛",(chess.ROOK,False):"♜",
             (chess.BISHOP,False):"♝",(chess.KNIGHT,False):"♞",(chess.PAWN,False):"♟"}
        return S.get((piece.piece_type, piece.color), "?")

    # ══════════════════════════════════════════════════════════════════════════
    #  MOUSE
    # ══════════════════════════════════════════════════════════════════════════

    def _on_hover(self, event):
        if self.spectating: return
        sq = self._xy_to_sq(event.x, event.y)
        if sq == self.hovered_sq: return
        self.hovered_sq = sq
        p = self.board.piece_at(sq)
        if p and p.color == self.my_color and self.board.turn == self.my_color:
            self.hints = {m.to_square for m in self.board.legal_moves if m.from_square == sq}
        else:
            self.hints = set()
        self._draw_board()

    def _on_leave(self, _):
        self.hovered_sq = None
        if not self.selected: self.hints = set(); self._draw_board()

    def _on_click(self, event):
        if self.spectating or self.board.turn != self.my_color: return
        sq = self._xy_to_sq(event.x, event.y)
        if self.selected is None:
            p = self.board.piece_at(sq)
            if p and p.color == self.my_color:
                self.selected = sq
                self.hints    = {m.to_square for m in self.board.legal_moves
                                 if m.from_square == sq}
        else:
            uci = chess.square_name(self.selected) + chess.square_name(sq)
            fp  = self.board.piece_at(self.selected)
            if fp and fp.piece_type == chess.PAWN and chess.square_rank(sq) in (0,7):
                uci += "q"
            try:
                if chess.Move.from_uci(uci) in self.board.legal_moves:
                    self._submit_move(uci)
            except Exception:
                pass
            self.selected = None; self.hints = set()
        self._draw_board()

    # ══════════════════════════════════════════════════════════════════════════
    #  VOICE
    # ══════════════════════════════════════════════════════════════════════════

    def _on_voice(self):
        if self.listening: return
        if self.board.turn != self.my_color:
            self._voice_status.config(text="Not your turn!", fg=DANGER); return
        self.listening = True
        self._voice_btn.config(text="🔴  Listening…")
        self._voice_status.config(text="Speak your move…", fg=FG)
        def _done(text):
            self.listening = False
            self.root.after(0, lambda: self._voice_btn.config(text="🎤  Speak Move"))
            uci = parse_voice_to_uci(text, self.board)
            if uci:
                h = uci_to_human(uci, self.board)
                self.root.after(0, lambda: self._voice_status.config(
                    text=f'"{text}" → {h}', fg=SUCCESS))
                self.root.after(0, lambda: self._submit_move(uci))
            else:
                self.root.after(0, lambda: self._voice_status.config(
                    text=f'Could not parse: "{text}"', fg=DANGER))
        listen_async(_done, duration=4.0)

    # ══════════════════════════════════════════════════════════════════════════
    #  MOVE SUBMIT
    # ══════════════════════════════════════════════════════════════════════════

    def _submit_move(self, uci: str):
        try:    move = chess.Move.from_uci(uci)
        except: return
        if move not in self.board.legal_moves:
            if hasattr(self, "_voice_status"):
                self._voice_status.config(text=f"Illegal: {uci}", fg=DANGER)
            return
        self.clock.press(self.board.turn == chess.WHITE)
        self.board.push(move)
        self.last_move = move
        self._move_uci_log.append(uci)
        self.client.move(uci)
        self._draw_board(); self._update_history(); self._update_info()
        self._check_end()
        if self.engine and self.board.turn != self.my_color and not self.board.is_game_over():
            threading.Thread(target=self._engine_reply, daemon=True).start()

    def _engine_reply(self):
        uci = self.engine.get_move(self.board)
        self.root.after(0, lambda: self._submit_move(uci))

    def _check_end(self):
        if self.board.is_checkmate():
            w = "black" if self.board.turn == chess.WHITE else "white"
            self._end("1-0" if w == "white" else "0-1", w)
        elif self.board.is_stalemate() or self.board.is_insufficient_material():
            self._end("1/2-1/2", None)

    def _end(self, result: str, winner):
        self.clock.pause()
        nw, nb = compute_rating_change(self._white_rating, self._black_rating, result)
        if hasattr(self, "_elo_lbl"):
            self._elo_lbl.config(
                text=f"ELO ⬜ {rating_diff_str(self._white_rating, nw)}  "
                     f"⬛ {rating_diff_str(self._black_rating, nb)}")
        if self.tournament:
            ms = self.tournament.get_current_matches()
            if ms:
                self.tournament.record_result(ms[0].match_id, winner, result,
                                              self._move_uci_log.copy())

    def _on_resign(self):
        if messagebox.askyesno("Resign", "Resign this game?"):
            self.client.resign(); self.clock.pause()

    def _on_flag(self, color: str):
        winner = "black" if color == "white" else "white"
        self.root.after(0, lambda: messagebox.showinfo(
            "Flag!", f"{color.capitalize()} flagged!\n{winner.capitalize()} wins on time."))

    # ══════════════════════════════════════════════════════════════════════════
    #  SERVER
    # ══════════════════════════════════════════════════════════════════════════

    def _on_server_message(self, msg: str):
        self.root.after(0, lambda: self._handle_msg(msg))

    def _handle_msg(self, msg: str):
        if msg.startswith("ASSIGNED:"):
            c = msg.split(":")[1]
            self.my_color = chess.WHITE if c == "white" else chess.BLACK
            if c != "spectator": self.clock.start_white()
            self._update_info()

        elif msg.startswith("MOVE:"):
            parts = msg.split(":", 2)
            if len(parts) == 3 and parts[1] != self.username:
                try:
                    m = chess.Move.from_uci(parts[2])
                    if m in self.board.legal_moves:
                        self.clock.press(self.board.turn == chess.WHITE)
                        self.board.push(m); self.last_move = m
                        self._move_uci_log.append(parts[2])
                        self._draw_board(); self._update_history()
                        self._update_info(); self._check_end()
                except Exception: pass

        elif msg.startswith("STATE:"):
            try:
                d = json.loads(msg[6:])
                self.board = chess.Board(d["fen"])
                if "white_ms" in d and not self.clock.unlimited:
                    self.clock.white_time = d["white_ms"] / 1000
                    self.clock.black_time = d["black_ms"] / 1000
                self._draw_board(); self._update_info()
            except Exception: pass

        elif msg.startswith("GAMEOVER:"):
            w = msg.split(":")[1]
            r = "1-0" if w == "white" else "0-1"
            self._end(r, w)
            messagebox.showinfo("Game Over",
                f"{'🏆 You won!' if w[0]==('w' if self.my_color==chess.WHITE else 'b') else '😢 You lost.'}\nWinner: {w}")

        elif msg.startswith("JOINED:") and hasattr(self, "_voice_status"):
            self._voice_status.config(text=f"{msg.split(':')[1]} joined", fg=SUCCESS)
        elif msg.startswith("LEFT:")  and hasattr(self, "_voice_status"):
            self._voice_status.config(text=f"{msg.split(':')[1]} left", fg=WARNING)
        elif msg.startswith("ERROR:") and hasattr(self, "_voice_status"):
            self._voice_status.config(text=msg[6:], fg=DANGER)

    # ══════════════════════════════════════════════════════════════════════════
    #  REPLAY + BRACKET
    # ══════════════════════════════════════════════════════════════════════════

    def _open_replay(self):
        if not self._move_uci_log:
            messagebox.showinfo("Replay", "No moves yet."); return
        ReplayViewer(self.root, moves=self._move_uci_log.copy(),
                     white=self.username if self.my_color == chess.WHITE else self.opponent,
                     black=self.username if self.my_color == chess.BLACK else self.opponent)

    def _show_bracket(self):
        if not self.tournament: return
        win = tk.Toplevel(self.root)
        win.title("🏆 Bracket"); win.configure(bg=BG); win.geometry("520x480")
        b = self.tournament.get_bracket()
        tk.Label(win, text=f"🏆 {b['name']}", font=("Helvetica", 14, "bold"),
                 bg=BG, fg=ACCENT).pack(pady=10)
        tk.Label(win, text=f"Round {b['current_round']}", font=("Helvetica", 11),
                 bg=BG, fg=FG).pack()
        nb = ttk.Notebook(win)
        nb.pack(fill="both", expand=True, padx=8, pady=8)
        for rnum, matches in b["rounds"].items():
            frm = tk.Frame(nb, bg=BG); nb.add(frm, text=f"Round {rnum}")
            for m in matches:
                res = f"  → {m['winner']}" if m["winner"] else "  (pending)"
                tk.Label(frm, text=f"  {m['white']}  vs  {m['black']}{res}",
                         bg=BG, fg=SUCCESS if m["winner"] else FG,
                         font=("Helvetica", 10), anchor="w").pack(fill="x", pady=3, padx=10)
        sf = tk.Frame(nb, bg=BG); nb.add(sf, text="Standings")
        hdr = tk.Frame(sf, bg=BTN_BG); hdr.pack(fill="x", padx=4, pady=4)
        for col, w_ in [("Rank",6),("Player",16),("Score",6),("W",4),("L",4),("D",4)]:
            tk.Label(hdr, text=col, bg=BTN_BG, fg=ACCENT,
                     font=("Courier", 9, "bold"), width=w_).pack(side="left")
        for s in b["standings"]:
            row_ = tk.Frame(sf, bg=BG); row_.pack(fill="x", padx=4)
            for val, w_ in [(s["rank"],6),(s["username"],16),(s["score"],6),
                            (s["wins"],4),(s["losses"],4),(s["draws"],4)]:
                tk.Label(row_, text=str(val), bg=BG, fg=FG,
                         font=("Courier", 9), width=w_).pack(side="left")
        champ = self.tournament.champion()
        if champ:
            tk.Label(win, text=f"🥇 Champion: {champ}",
                     font=("Helvetica", 12, "bold"), bg=BG, fg=ACCENT).pack(pady=6)

    # ══════════════════════════════════════════════════════════════════════════
    #  HELPERS
    # ══════════════════════════════════════════════════════════════════════════

    def _update_info(self):
        if not hasattr(self, "_info_lbl"): return
        cs  = "White ♔" if self.my_color==chess.WHITE else "Black ♚" if self.my_color==chess.BLACK else "Spectator 👁"
        opp = self.opponent or ("Engine 🤖" if self.engine else "Waiting…")
        ms  = "PvP" if self.mode=="pvp" else "Engine" if self.mode=="engine" else "Tournament 🏆" if self.tournament else "—"
        self._info_lbl.config(text=f"You:  {self.username} ({cs})\nOpp:  {opp}\nMode: {ms}")
        if hasattr(self, "_turn_lbl"):
            t = ("Checkmate! 🏁" if self.board.is_checkmate() else
                 "Stalemate 🤝"  if self.board.is_stalemate() else
                 "⚠️ CHECK!"      if self.board.is_check() else
                 "White's turn"   if self.spectating and self.board.turn==chess.WHITE else
                 "Black's turn"   if self.spectating else
                 "Your turn ✅"   if self.board.turn==self.my_color else "Waiting ⏳")
            self._turn_lbl.config(text=t)

    def _update_history(self):
        if not hasattr(self, "_history_box"): return
        moves = list(self.board.move_stack)
        lines = [f"{i//2+1:>3}. {moves[i].uci():<8}{moves[i+1].uci() if i+1<len(moves) else ''}"
                 for i in range(0, len(moves), 2)]
        self._history_box.config(state="normal")
        self._history_box.delete("1.0", "end")
        self._history_box.insert("end", "\n".join(lines))
        self._history_box.see("end")
        self._history_box.config(state="disabled")
