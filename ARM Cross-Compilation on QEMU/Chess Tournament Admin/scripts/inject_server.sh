#!/usr/bin/env bash
# inject_server.sh — Cross-compile chess server and inject into Buildroot rootfs
set -e

ROOTFS="$HOME/embedded-linux/buildroot-2024.02/output/images/rootfs.ext2"
SERVER_DIR="$(dirname "$0")/../server"
MOUNT="/mnt/rootfs"

echo "[*] Building ARM chess server..."
cd "$SERVER_DIR"
make clean && make

echo "[*] Injecting into rootfs..."
sudo mkdir -p "$MOUNT"
sudo mount -o loop "$ROOTFS" "$MOUNT"
sudo cp chess_server "$MOUNT/root/"
sudo chmod +x "$MOUNT/root/chess_server"
sudo umount "$MOUNT"

echo "[+] Done! chess_server is at /root/chess_server inside the image."
echo "    Boot QEMU and run: /root/chess_server 5000"
