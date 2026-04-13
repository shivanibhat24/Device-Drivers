#!/usr/bin/env bash
# boot_qemu.sh — Launch QEMU ARM vexpress with chess server port forwarding
set -e

BUILDROOT="$HOME/embedded-linux/buildroot-2024.02/output/images"
KERNEL="$BUILDROOT/zImage"
ROOTFS="$BUILDROOT/rootfs.ext2"
DTB="$BUILDROOT/vexpress-v2p-ca9.dtb"

echo "[*] Booting QEMU ARM vexpress..."
echo "[*] Port forwards:  host:5555 → guest:5000 (chess server)"
echo "                    host:1234 → guest:1234 (gdbserver)"

qemu-system-arm \
  -M vexpress-a9 \
  -cpu cortex-a9 \
  -m 256M \
  -kernel "$KERNEL" \
  -dtb    "$DTB" \
  -drive  "file=$ROOTFS,format=raw" \
  -append "root=/dev/mmcblk0 rw console=ttyAMA0 rootwait" \
  -nographic \
  -netdev user,id=net0,hostfwd=tcp::5555-:5000,hostfwd=tcp::1234-:1234 \
  -device virtio-net-device,netdev=net0
