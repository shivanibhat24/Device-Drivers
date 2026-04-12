# Linux Kernel Driver for QEMU EDU Device

A complete embedded Linux kernel driver development project targeting the **QEMU EDU virtual PCI device** on an emulated **ARM64 (virt) board**.

This project demonstrates real-world BSP/driver engineering skills:
- Cross-compilation for ARM64 on a Windows/WSL2 host
- Linux kernel build from source (v6.6)
- PCI driver with MMIO register access, IRQ handling, and character device
- Custom minimal Linux rootfs (BusyBox + Dropbear SSH)
- Full QEMU bring-up with network and VS Code remote development

---

## Project Structure

```
edu-driver-project/
├── driver/
│   ├── edu_driver.c      # PCI kernel module (main driver)
│   ├── Makefile          # Out-of-tree kernel module build
│   └── test_edu.c        # Userspace test application
├── scripts/
│   ├── init              # Guest init script (PID 1)
│   ├── build.sh          # Build driver + pack initramfs
│   └── run_qemu.sh       # Launch QEMU guest
├── .vscode/
│   ├── tasks.json        # Build tasks (Ctrl+Shift+B)
│   ├── c_cpp_properties.json  # IntelliSense for kernel headers
│   └── ssh_config_snippet.txt # Add to ~/.ssh/config for Remote-SSH
└── README.md
```

---

## Host Requirements

| Tool | Version tested | Install |
|---|---|---|
| Windows 11 | 22H2+ | — |
| WSL2 (Ubuntu 22.04) | 2.x | `wsl --install` |
| QEMU for Windows | 10.x | [qemu.org](https://www.qemu.org/download/) |
| VS Code | latest | [code.visualstudio.com](https://code.visualstudio.com/) |
| gcc-aarch64-linux-gnu | 11.x | `sudo apt install gcc-aarch64-linux-gnu` |

> All build steps run inside **WSL2**. QEMU runs as a Windows binary (`qemu-system-aarch64.exe`) called from WSL2 via Windows PATH interop.

---

## Quick Start

### 1 — Install dependencies (WSL2)

```bash
sudo apt update && sudo apt install -y \
  gcc-aarch64-linux-gnu g++-aarch64-linux-gnu \
  binutils-aarch64-linux-gnu libssl-dev libelf-dev \
  flex bison bc git wget cpio gzip make
```

### 2 — Build the Linux kernel

```bash
cd ~
git clone --depth=1 --branch v6.6 \
  https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git
cd linux
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- defconfig
make -j$(nproc) ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-
```

Expected output: `arch/arm64/boot/Image is ready` (~30-50 minutes on first build)

### 3 — Build BusyBox rootfs

```bash
cd ~
wget https://busybox.net/downloads/busybox-1.36.1.tar.bz2
tar xf busybox-1.36.1.tar.bz2 && cd busybox-1.36.1
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- defconfig
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- CONFIG_STATIC=y -j$(nproc)
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- CONFIG_STATIC=y install
```

### 4 — Build Dropbear SSH server (static)

```bash
cd ~
wget https://matt.ucc.asn.au/dropbear/releases/dropbear-2022.83.tar.bz2
tar xf dropbear-2022.83.tar.bz2 && cd dropbear-2022.83
# Disable password auth (requires crypt() — not available in cross-build)
echo "#define DROPBEAR_SVR_PASSWORD_AUTH 0" > localoptions.h
./configure --host=aarch64-linux-gnu --disable-zlib --disable-pam \
  CC=aarch64-linux-gnu-gcc
make PROGRAMS="dropbear dropbearkey scp" STATIC=1 LDFLAGS="-static" -j$(nproc)
```

### 5 — Assemble the rootfs

```bash
cd ~
mkdir -p rootfs/{bin,sbin,etc,proc,sys,dev,lib,lib64,tmp,home,var/run,var/log}
mkdir -p rootfs/{lib/modules,usr/sbin,usr/bin,root/.ssh}

# BusyBox
cp -a busybox-1.36.1/_install/* rootfs/

# Dropbear
cp dropbear-2022.83/dropbear    rootfs/usr/sbin/
cp dropbear-2022.83/dropbearkey rootfs/usr/bin/
cp dropbear-2022.83/scp         rootfs/usr/bin/

# SSH key (generate once)
ssh-keygen -t rsa -b 2048 -f ~/.ssh/qemu_guest -N ""
cp ~/.ssh/qemu_guest.pub rootfs/root/.ssh/authorized_keys
chmod 700 rootfs/root/.ssh
chmod 600 rootfs/root/.ssh/authorized_keys

# Copy init script
cp scripts/init rootfs/init
chmod +x rootfs/init
```

### 6 — Build the driver and pack initramfs

```bash
# Build driver
make -C driver KDIR=~/linux ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-

# Copy into rootfs
cp driver/edu_driver.ko rootfs/lib/modules/

# Build and copy test app
aarch64-linux-gnu-gcc -static -o rootfs/bin/test_edu driver/test_edu.c

# Pack
cd rootfs
find . | cpio -o -H newc | gzip > ~/initramfs.cpio.gz
```

Or simply run:
```bash
bash scripts/build.sh
```

### 7 — Boot in QEMU

```bash
bash scripts/run_qemu.sh
```

Or manually:
```bash
qemu-system-aarch64.exe \
  -machine virt \
  -cpu cortex-a57 \
  -m 512M \
  -kernel ~/linux/arch/arm64/boot/Image \
  -initrd ~/initramfs.cpio.gz \
  -append "console=ttyAMA0 rdinit=/init" \
  -device edu \
  -netdev user,id=net0,hostfwd=tcp::2222-:22 \
  -device virtio-net-device,netdev=net0 \
  -nographic
```

To exit QEMU: `Ctrl-A` then `X`

---

## Testing the Driver

Inside the QEMU guest console:

```sh
# Confirm device node exists
ls -la /dev/edu0

# Read the live hardware clock register
cat /dev/edu0
# Output: liveclock=0x00001234

# Compute 5! (triggers MMIO write + IRQ)
echo "5" > /dev/edu0

# Check kernel log for result
dmesg | grep edu
# edu 0000:00:01.0: Computing 5!
# edu 0000:00:01.0: IRQ received! status=0x1
# edu 0000:00:01.0: Result: 120

# Run the full test suite
/bin/test_edu
```

---

## VS Code Remote Development

### Add SSH config (Windows)

Copy the contents of `.vscode/ssh_config_snippet.txt` into `C:\Users\<YourName>\.ssh\config`.

### Connect

1. Open VS Code
2. `Ctrl+Shift+P` → **Remote-SSH: Connect to Host**
3. Select **qemu-guest**
4. VS Code opens a full editor session inside the running ARM64 guest

### Build shortcut

With the project open in WSL2 (via the WSL extension):
- `Ctrl+Shift+B` → builds `edu_driver.ko`

To deploy a new build to the running guest:

```bash
# Copy module
scp -P 2222 -i ~/.ssh/qemu_guest driver/edu_driver.ko root@localhost:/lib/modules/

# Reload inside guest
ssh -p 2222 -i ~/.ssh/qemu_guest root@localhost \
  'rmmod edu_driver; insmod /lib/modules/edu_driver.ko'
```

---

## How It Works

```
User space          Kernel space              QEMU (host)
──────────          ────────────              ──────────
echo "5"     →   edu_write()            →   EDU MMIO reg
/dev/edu0        iowrite32(n, FACTORIAL)     Computes N!
                                             Raises IRQ
             ←   edu_irq_handler()     ←
                 ioread32(FACTORIAL)
                 dev_info("Result: 120")
dmesg        ←   kernel log
```

### Key driver concepts demonstrated

| Concept | Code location |
|---|---|
| PCI device enumeration | `edu_probe()` / `pci_enable_device()` |
| MMIO register mapping | `pci_iomap()` / `ioread32()` / `iowrite32()` |
| Interrupt handling | `request_irq()` / `edu_irq_handler()` |
| Character device | `cdev_init()` / `alloc_chrdev_region()` |
| Sysfs /dev node | `class_create()` / `device_create()` |
| Error unwinding | `goto` chain in `edu_probe()` |

---

## EDU Device Register Map

| Offset | Name | Access | Description |
|---|---|---|---|
| `0x00` | ID | R | Device identification (`0x010000ed`) |
| `0x04` | LIVECLOCK | R | Free-running counter |
| `0x08` | FACTORIAL | R/W | Write N, read N! |
| `0x20` | STATUS | R/W | Bit 0: BUSY, Bit 7: IRQ_EN |
| `0x24` | IRQ_STATUS | R | Interrupt status flags |
| `0x60` | IRQ_RAISE | W | Write any value to raise IRQ |
| `0x64` | IRQ_ACK | W | Write status value to acknowledge IRQ |

---

## What's Next — Extending This Project

| Extension | Skills gained |
|---|---|
| Add DMA support | Kernel DMA API, scatter-gather |
| virtio-i2c driver | I2C subsystem, `i2c_adapter` |
| pl011 UART driver | tty subsystem, serial comms |
| Port to Raspberry Pi 4 | Real hardware bring-up |
| Add device tree overlay | `.dts`, `platform_driver`, `of_match_table` |
| Yocto BSP layer | `.bb` recipe, custom image, SDK |

---

## Verified Output

```
[    3.576995] edu_driver: loading out-of-tree module taints kernel.
[    3.593773] edu 0000:00:01.0: enabling device (0000 -> 0002)
[    3.598717] edu 0000:00:01.0: EDU device found! ID=0x010000ed
[    3.603832] edu 0000:00:01.0: edu0 ready at /dev/edu0
[  106.560564] edu 0000:00:01.0: Computing 5!
[  106.563253] edu 0000:00:01.0: IRQ received! status=0x1
[  106.565575] edu 0000:00:01.0: Result: 120
```

**Kernel:** Linux 6.6.0  
**Architecture:** ARM64 (cortex-a57)  
**Board:** QEMU `virt`  
**Host:** Windows 11 + WSL2 Ubuntu 22.04  
**QEMU:** 10.2.50  

---

## License

GPL-2.0 — same license as the Linux kernel.
