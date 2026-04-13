#!/usr/bin/env bash
# gdb_session.sh — Attach gdb-multiarch to chess server running in QEMU
# Inside QEMU first run: gdbserver :1234 /root/chess_server

BINARY="$(dirname "$0")/../server/chess_server"

echo "[*] Attaching gdb-multiarch to localhost:1234"
echo "    Make sure gdbserver is running inside QEMU first:"
echo "    (qemu) gdbserver :1234 /root/chess_server"
echo ""

gdb-multiarch "$BINARY" \
  -ex "set architecture arm" \
  -ex "target remote :1234" \
  -ex "break main" \
  -ex "continue"
