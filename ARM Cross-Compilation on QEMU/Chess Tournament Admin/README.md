# ♟ Chess ARM Tournament

A virtual chess tournament organizer running a **Boost.Asio TCP game server on an emulated ARM board (QEMU)**, with a **Python GUI client** featuring **Whisper AI voice commands**, **Lichess API verification**, and **UCI engine loading from GitHub**.

---

## Architecture

```
WSL2 Host (x86_64)
├── Python GUI (tkinter)
│   ├── Lichess username verification
│   ├── Whisper AI voice → "Knight to F5" → UCI move
│   ├── NLP move parser
│   ├── Engine loader (clone GitHub repo → UCI)
│   └── Boost.Asio TCP Client → port 5555
│                                    │
QEMU ARM vexpress-a9                 │ TCP :5555 → :5000
├── Buildroot Linux                  │
├── Boost.Asio TCP Server ◄──────────┘
│   ├── Multi-threaded async sessions
│   ├── Game state (FEN, move history, turns)
│   └── Broadcasts moves to all clients
└── gdbserver :1234 ◄──── gdb-multiarch (WSL2)
```

---

## Stack

| Layer | Technology |
|---|---|
| Game Server | C++17, Boost.Asio |
| Target Platform | QEMU ARM vexpress-a9, Buildroot Linux |
| Cross Compiler | arm-linux-gnueabihf-g++ |
| GUI | Python, tkinter |
| Voice Input | OpenAI Whisper |
| Move Parsing | python-chess + custom NLP |
| Auth | Lichess public API |
| Engine | Any UCI engine via GitHub URL |
| Debugger | gdb-multiarch + gdbserver |

---

## Prerequisites

### WSL2 / Ubuntu 22.04
```bash
sudo apt install -y gcc-arm-linux-gnueabihf g++-arm-linux-gnueabihf \
                    qemu-system-arm gdb-multiarch \
                    libboost-all-dev build-essential wget
```

### Python
```bash
pip install -r client/requirements.txt --break-system-packages
```

### Buildroot
```bash
cd ~/embedded-linux
wget https://buildroot.org/downloads/buildroot-2024.02.tar.gz
tar xf buildroot-2024.02.tar.gz
cd buildroot-2024.02
make qemu_arm_vexpress_defconfig
make menuconfig   # Enable: C++, boost, gdb, gdbserver
make -j$(nproc)
```

---

## Quick Start

### 1. Build & inject server into rootfs
```bash
chmod +x scripts/*.sh
./scripts/inject_server.sh
```

### 2. Boot QEMU
```bash
./scripts/boot_qemu.sh
```

Inside QEMU, start the server:
```bash
/root/chess_server 5000
```

### 3. Launch GUI (WSL2)
```bash
cd client
python main.py
```

Enter your Lichess username, connect to `127.0.0.1:5555`, choose your opponent, and play!

### 4. Remote debug with GDB (optional)
Inside QEMU:
```bash
gdbserver :1234 /root/chess_server 5000
```
On WSL2:
```bash
./scripts/gdb_session.sh
```

---

## Server Protocol

All messages are newline-terminated ASCII.

| Direction | Message | Description |
|---|---|---|
| Client → Server | `AUTH:<username>` | Register player |
| Client → Server | `MOVE:<uci>` | e.g. `MOVE:e2e4` |
| Client → Server | `RESIGN` | Forfeit |
| Client → Server | `STATUS` | Request state |
| Client → Server | `MODE:pvp\|engine` | Set game mode |
| Server → Client | `ASSIGNED:white\|black\|spectator` | Color assignment |
| Server → Client | `MOVE:<user>:<uci>` | Move broadcast |
| Server → Client | `STATE:<json>` | Full game state |
| Server → Client | `GAMEOVER:<winner>` | Game end |
| Server → Client | `ERROR:<reason>` | Error message |

---

## Voice Commands

Say anything natural after clicking 🎤:

| You say | Parsed as |
|---|---|
| "Knight to F5" | Knight → f5 |
| "E2 to E4" | e2e4 |
| "Castle kingside" | e1g1 |
| "Pawn takes E5" | xE5 with pawn |
| "Bishop Charlie 4" | Bishop → c4 |

---

## Engine Loading

Enter any GitHub repo URL that contains a UCI-compatible chess engine binary. The loader will:
1. Clone the repo (`git clone --depth=1`)
2. Attempt `make` or `cmake` build
3. Find the executable binary
4. Wrap it with `python-chess` UCI interface

Example repos:
- `https://github.com/official-stockfish/Stockfish`
- `https://github.com/AndyGrant/Ethereal`
- `https://github.com/drstrange11/sunfish` (Python, simple)

---

## Project Structure

```
chess-arm-tournament/
├── server/
│   ├── server.cpp          Boost.Asio TCP game server
│   ├── game_state.hpp      Board state, players, FEN
│   └── Makefile            ARM cross-compile + rootfs inject
├── client/
│   ├── main.py             Entry point
│   ├── gui.py              tkinter chessboard GUI
│   ├── tcp_client.py       Async TCP client
│   ├── lichess_auth.py     Lichess API verification
│   ├── nlp_parser.py       Voice → UCI move parser
│   ├── voice.py            Whisper recording + transcription
│   ├── engine_loader.py    GitHub clone + UCI engine wrapper
│   └── requirements.txt
├── scripts/
│   ├── boot_qemu.sh        Launch QEMU with port forwarding
│   ├── inject_server.sh    Cross-compile + inject into rootfs
│   └── gdb_session.sh      Attach GDB remote debugger
└── README.md
```

---

## Contributing

PRs welcome! Some ideas:
- [ ] Chess clock / time controls
- [ ] Spectator mode in GUI
- [ ] ELO rating updates via Lichess API
- [ ] Move suggestions (show legal moves on hover)
- [ ] Tournament bracket (multiple games)
- [ ] Replay viewer

---

## License

MIT — built with ❤️ by Shivani Bhat
