"""
TCP client for chess-arm-tournament.
Connects to the Boost.Asio server running inside QEMU ARM.
"""
import socket
import threading


class ChessTCPClient:
    def __init__(self, host: str = "127.0.0.1", port: int = 5555):
        self.host = host
        self.port = port
        self.sock = None
        self.connected = False
        self.on_message = None   # callable(str) — set by GUI

    # ── Connection ────────────────────────────────────────
    def connect(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.connect((self.host, self.port))
        self.connected = True
        print(f"[TCP] Connected to {self.host}:{self.port}")
        threading.Thread(target=self._listen, daemon=True).start()

    def disconnect(self):
        self.connected = False
        if self.sock:
            self.sock.close()

    # ── Send helpers ──────────────────────────────────────
    def send(self, msg: str):
        if self.connected:
            self.sock.sendall((msg.strip() + "\n").encode())

    def auth(self, username: str):
        self.send(f"AUTH:{username}")

    def move(self, uci: str):
        self.send(f"MOVE:{uci}")

    def resign(self):
        self.send("RESIGN")

    def set_mode(self, mode: str):
        """mode: 'pvp' or 'engine'"""
        self.send(f"MODE:{mode}")

    def request_status(self):
        self.send("STATUS")

    # ── Receive loop ──────────────────────────────────────
    def _listen(self):
        buf = ""
        while self.connected:
            try:
                data = self.sock.recv(4096).decode()
                if not data:
                    break
                buf += data
                while "\n" in buf:
                    line, buf = buf.split("\n", 1)
                    line = line.strip()
                    if line and self.on_message:
                        self.on_message(line)
            except Exception as e:
                if self.connected:
                    print(f"[TCP] Recv error: {e}")
                break
        self.connected = False
        print("[TCP] Disconnected")
