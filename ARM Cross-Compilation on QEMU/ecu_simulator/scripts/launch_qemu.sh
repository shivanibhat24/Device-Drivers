#!/usr/bin/env bash
# ─────────────────────────────────────────────────────
#  launch_qemu.sh
#
#  Starts the ECU firmware in QEMU (sifive_u RISC-V)
#  and bridges both communication channels to Windows:
#
#    UART0 (CAN frames) → PTY → socat → TCP :5002
#                                        → Qt reads as serial via COM port
#    UART1 (control)    → TCP :5001      → Qt connects directly via TCP
#
#  QEMU args:
#    -serial pty              UART0 gets a PTY like /dev/pts/3
#    -serial tcp::5001,...    UART1 becomes a raw TCP server on :5001
#
#  Run this script in WSL before starting the Qt GUI on Windows.
# ─────────────────────────────────────────────────────

set -e

FIRMWARE="${1:-../firmware/build/ecu_firmware}"
QEMU="${QEMU_PATH:-qemu-system-riscv64}"
CAN_BRIDGE_PORT=5002
CTRL_PORT=5001

# ── Sanity checks ─────────────────────────────────────
if ! command -v "$QEMU" &>/dev/null; then
    echo "[ERROR] qemu-system-riscv64 not found."
    echo "        Install: sudo apt install qemu-system-misc"
    exit 1
fi

if [[ ! -f "$FIRMWARE" ]]; then
    echo "[ERROR] Firmware not found at: $FIRMWARE"
    echo "        Build first: cd firmware && mkdir build && cd build"
    echo "        cmake -DCMAKE_TOOLCHAIN_FILE=../toolchain-riscv.cmake .. && make"
    exit 1
fi

# ── Start QEMU in background ──────────────────────────
echo "[LAUNCH] Starting QEMU..."
"$QEMU" \
    -machine sifive_u \
    -nographic \
    -bios none \
    -kernel "$FIRMWARE" \
    -serial pty \
    -serial "tcp::${CTRL_PORT},server,nowait" \
    2>&1 | tee /tmp/qemu_ecu.log &

QEMU_PID=$!
echo "[LAUNCH] QEMU PID: $QEMU_PID"

# ── Wait for QEMU to create the PTY ───────────────────
echo "[LAUNCH] Waiting for UART0 PTY..."
PTY_PATH=""
for i in $(seq 1 20); do
    PTY_PATH=$(grep -oP 'char device redirected to \K/dev/pts/\d+' \
               /tmp/qemu_ecu.log 2>/dev/null | head -1)
    if [[ -n "$PTY_PATH" ]]; then break; fi
    sleep 0.5
done

if [[ -z "$PTY_PATH" ]]; then
    echo "[ERROR] Could not detect PTY path from QEMU output."
    echo "        Check /tmp/qemu_ecu.log"
    kill $QEMU_PID 2>/dev/null
    exit 1
fi
echo "[LAUNCH] UART0 PTY: $PTY_PATH"

# ── Bridge PTY → TCP :5002 with socat ─────────────────
if command -v socat &>/dev/null; then
    echo "[LAUNCH] Bridging $PTY_PATH → TCP :$CAN_BRIDGE_PORT with socat..."
    socat "TCP-LISTEN:${CAN_BRIDGE_PORT},reuseaddr,fork" \
          "FILE:${PTY_PATH},rawer" &
    SOCAT_PID=$!
    echo "[LAUNCH] socat PID: $SOCAT_PID"
else
    echo "[WARN] socat not found — PTY bridge not started."
    echo "       Install: sudo apt install socat"
    echo "       The Qt GUI can connect to the PTY directly on WSL,"
    echo "       or install socat and re-run."
fi

# ── Print connection info ─────────────────────────────
WSL_IP=$(ip route show | grep -oP 'src \K[\d.]+' | head -1)

echo ""
echo "════════════════════════════════════════════"
echo "  ECU Simulator running in QEMU"
echo "════════════════════════════════════════════"
echo "  Control (TCP):  ${WSL_IP}:${CTRL_PORT}"
echo "  CAN stream:     ${WSL_IP}:${CAN_BRIDGE_PORT}  (socat bridge)"
echo "  UART0 PTY:      ${PTY_PATH}"
echo ""
echo "  In Qt GUI → Connect to QEMU:"
echo "    Host: ${WSL_IP}"
echo "    Control port: ${CTRL_PORT}"
echo "    Serial port: Use a COM port from:"
echo "      scripts/create_com_bridge.sh"
echo "════════════════════════════════════════════"
echo ""
echo "Press Ctrl+C to stop QEMU and socat."

# ── Wait for QEMU to exit ─────────────────────────────
wait $QEMU_PID
echo "[LAUNCH] QEMU exited."
kill $SOCAT_PID 2>/dev/null || true
