#!/usr/bin/env bash
# ─────────────────────────────────────────────────────
#  create_com_bridge.sh
#
#  Bridges the QEMU UART0 PTY to a Windows named pipe,
#  which Qt sees as a COM port (e.g. \\.\pipe\ecu_can).
#
#  Requires: npiperelay.exe (from Windows side)
#    https://github.com/jstarks/npiperelay
#    Place npiperelay.exe in /mnt/c/tools/ or adjust path.
#
#  Usage: ./create_com_bridge.sh /dev/pts/3
# ─────────────────────────────────────────────────────

PTY="${1}"
NPIPERELAY="${NPIPERELAY_PATH:-/mnt/c/tools/npiperelay.exe}"
PIPE_NAME="ecu_can"

if [[ -z "$PTY" ]]; then
    echo "Usage: $0 /dev/pts/<N>"
    echo "  PTY path is printed by launch_qemu.sh"
    exit 1
fi

if [[ ! -f "$NPIPERELAY" ]]; then
    echo "[ERROR] npiperelay.exe not found at $NPIPERELAY"
    echo "        Download from https://github.com/jstarks/npiperelay/releases"
    echo "        and place at /mnt/c/tools/npiperelay.exe"
    exit 1
fi

echo "[BRIDGE] Connecting $PTY → \\\\.\\pipe\\$PIPE_NAME"
echo "[BRIDGE] In Qt GUI, use serial port: \\\\.\\pipe\\$PIPE_NAME"
echo ""
echo "Press Ctrl+C to stop."

socat \
    "EXEC:\"${NPIPERELAY} -ep -s //./pipe/${PIPE_NAME}\",nofork" \
    "FILE:${PTY},rawer"
