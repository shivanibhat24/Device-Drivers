#!/usr/bin/env bash
# ─────────────────────────────────────────────────────
#  build_firmware.sh
#  Builds the ECU firmware for QEMU RISC-V.
#  Run from the repo root or firmware/ directory.
# ─────────────────────────────────────────────────────
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FIRMWARE_DIR="$SCRIPT_DIR/../firmware"
BUILD_DIR="$FIRMWARE_DIR/build"

echo "[BUILD] ECU Simulator Firmware"
echo "[BUILD] Source: $FIRMWARE_DIR"
echo "[BUILD] Output: $BUILD_DIR"

# Check toolchain
if ! command -v riscv64-unknown-elf-gcc &>/dev/null; then
    echo "[ERROR] riscv64-unknown-elf-gcc not found."
    echo "        Install: sudo apt install gcc-riscv64-unknown-elf"
    exit 1
fi

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

cmake \
    -DCMAKE_TOOLCHAIN_FILE="$FIRMWARE_DIR/toolchain-riscv.cmake" \
    -DCMAKE_BUILD_TYPE=Debug \
    -G Ninja \
    "$FIRMWARE_DIR"

ninja -j"$(nproc)"

echo ""
echo "[BUILD] ✔ Done"
echo "[BUILD] ELF:    $BUILD_DIR/ecu_firmware"
echo "[BUILD] Binary: $BUILD_DIR/ecu_firmware.bin"
echo ""
echo "[BUILD] Run with: ./scripts/launch_qemu.sh $BUILD_DIR/ecu_firmware"
