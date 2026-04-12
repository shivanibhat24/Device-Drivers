#!/bin/bash
# run_qemu.sh — Launch the QEMU ARM64 guest
# Run this inside WSL2. QEMU must be installed on Windows and in PATH.

KERNEL="$HOME/linux/arch/arm64/boot/Image"
INITRD="$HOME/initramfs.cpio.gz"

# Sanity checks
if [ ! -f "$KERNEL" ]; then
    echo "ERROR: Kernel not found at $KERNEL"
    echo "       Build it first: cd ~/linux && make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -j\$(nproc)"
    exit 1
fi

if [ ! -f "$INITRD" ]; then
    echo "ERROR: Initramfs not found at $INITRD"
    echo "       Run scripts/build.sh first"
    exit 1
fi

echo "Starting QEMU ARM64 guest..."
echo "  Kernel : $KERNEL"
echo "  Initrd : $INITRD"
echo "  SSH    : localhost:2222"
echo ""
echo "  To connect VS Code: Remote-SSH → qemu-guest"
echo "  To exit QEMU      : Ctrl-A then X"
echo ""

qemu-system-aarch64.exe \
    -machine virt \
    -cpu cortex-a57 \
    -m 512M \
    -kernel "$KERNEL" \
    -initrd "$INITRD" \
    -append "console=ttyAMA0 rdinit=/init" \
    -device edu \
    -netdev user,id=net0,hostfwd=tcp::2222-:22 \
    -device virtio-net-device,netdev=net0 \
    -nographic
