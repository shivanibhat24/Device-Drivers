#!/bin/bash
# build.sh — Full build script for the EDU driver project
#
# Run this inside WSL2 (Ubuntu 22.04) after cloning the repo.
# Prerequisites: sudo apt install gcc-aarch64-linux-gnu make bc flex bison libssl-dev libelf-dev

set -e  # Exit on any error

KERNEL_DIR="$HOME/linux"
ROOTFS_DIR="$HOME/rootfs"
DRIVER_DIR="$(dirname "$0")/driver"
ARCH=arm64
CROSS=aarch64-linux-gnu-

# -----------------------------------------------------------------------
# Step 1 — Build the kernel module
# -----------------------------------------------------------------------
echo "[1/4] Building edu_driver.ko..."
make -C "$DRIVER_DIR" KDIR="$KERNEL_DIR" ARCH=$ARCH CROSS_COMPILE=$CROSS
echo "      OK: $DRIVER_DIR/edu_driver.ko"

# -----------------------------------------------------------------------
# Step 2 — Cross-compile the test application
# -----------------------------------------------------------------------
echo "[2/4] Building test_edu..."
${CROSS}gcc -static -o "$DRIVER_DIR/test_edu" "$DRIVER_DIR/test_edu.c"
echo "      OK: $DRIVER_DIR/test_edu"

# -----------------------------------------------------------------------
# Step 3 — Copy binaries into rootfs
# -----------------------------------------------------------------------
echo "[3/4] Updating rootfs..."
mkdir -p "$ROOTFS_DIR/lib/modules"
mkdir -p "$ROOTFS_DIR/bin"
cp "$DRIVER_DIR/edu_driver.ko" "$ROOTFS_DIR/lib/modules/"
cp "$DRIVER_DIR/test_edu"      "$ROOTFS_DIR/bin/"
echo "      OK: copied to $ROOTFS_DIR"

# -----------------------------------------------------------------------
# Step 4 — Rebuild the initramfs
# -----------------------------------------------------------------------
echo "[4/4] Rebuilding initramfs..."
cd "$ROOTFS_DIR"
find . | cpio -o -H newc | gzip > "$HOME/initramfs.cpio.gz"
echo "      OK: $HOME/initramfs.cpio.gz ($(du -sh $HOME/initramfs.cpio.gz | cut -f1))"

echo ""
echo "Build complete! Run QEMU with:"
echo ""
echo "  qemu-system-aarch64.exe \\"
echo "    -machine virt \\"
echo "    -cpu cortex-a57 \\"
echo "    -m 512M \\"
echo "    -kernel ~/linux/arch/arm64/boot/Image \\"
echo "    -initrd ~/initramfs.cpio.gz \\"
echo "    -append \"console=ttyAMA0 rdinit=/init\" \\"
echo "    -device edu \\"
echo "    -netdev user,id=net0,hostfwd=tcp::2222-:22 \\"
echo "    -device virtio-net-device,netdev=net0 \\"
echo "    -nographic"
