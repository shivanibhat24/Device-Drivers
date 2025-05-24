# FirmWatch - Hot-Reloadable Firmware Blob Manager

FirmWatch is a Linux kernel module that provides hot-reloading of firmware blobs in kernel memory space through a memory-mapped interface. It allows applications to dynamically load, unload, and access firmware without system reboots.

## Features

- **Hot-reload firmware**: Load new firmware versions without rebooting
- **Memory-mapped access**: Direct mmap interface for high-performance firmware access
- **Multiple slots**: Support for up to 256 simultaneous firmware blobs
- **Reference counting**: Safe unloading with active mapping protection
- **Proc interface**: Real-time status monitoring via `/proc/firmwatch`
- **Standard firmware loader**: Integrates with Linux firmware loading infrastructure

## Architecture

```
┌─────────────────┐    ┌──────────────────┐    ┌─────────────────┐
│   Application   │    │   firmwatch-util │    │ Kernel Module   │
│                 │◄──►│                  │◄──►│  /dev/firmwatch │
│   mmap()        │    │   ioctl()        │    │                 │
└─────────────────┘    └──────────────────┘    └─────────────────┘
                                                         │
                                                         ▼
                                               ┌─────────────────┐
                                               │ Firmware Slots  │
                                               │ [0] [1] ... [N] │
                                               └─────────────────┘
```

## Building and Installation

### Prerequisites
- Linux kernel headers for your running kernel
- GCC compiler
- Make

### Build
```bash
# Build both kernel module and userspace utility
make all

# Or build individually
make module     # Kernel module only
make userspace  # Userspace utility only
```

### Installation
```bash
# Install system-wide (requires root)
sudo make install

# Load the kernel module
sudo make load
```

### Verification
```bash
# Check if module is loaded
lsmod | grep firmwatch

# Verify device node exists
ls -l /dev/firmwatch

# Check proc interface
cat /proc/firmwatch
```

## Usage

### Command Line Interface

The `firmwatch-util` command provides a simple interface to manage firmware:

```bash
# Load firmware (auto-assign slot)
firmwatch-util load my_firmware.bin

# Load firmware into specific slot
firmwatch-util load my_firmware.bin 5

# List all loaded firmware
firmwatch-util list

# Get detailed info about a slot
firmwatch-util info 5

# Memory map and dump firmware to file
firmwatch-util mmap 5 /tmp/firmware_dump.bin

# Watch slot for hot-reload changes
firmwatch-util watch 5

# Unload firmware
firmwatch-util unload 5
```

### Hot-Reload Demonstration

1. **Load initial firmware:**
```bash
firmwatch-util load firmware_v1.bin 0
```

2. **Start monitoring in another terminal:**
```bash
firmwatch-util watch 0
```

3. **Create application that maps the firmware:**
```c
#include <sys/mman.h>
#include <fcntl.h>

int fd = open("/dev/firmwatch", O_RDWR);
void *firmware = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0); // slot 0
```

4. **Hot-reload with new firmware:**
```bash
firmwatch-util load firmware_v2.bin 0  # Same slot
```

5. **Observer sees the change:**
```
FIRMWARE RELOADED: firmware_v2.bin (8192 bytes)
```

The application can detect the change and remap if needed, enabling seamless firmware updates.

### Programming Interface

#### IOCTL Commands

```c
#include <sys/ioctl.h>

#define FIRMWATCH_IOC_MAGIC 'F'
#define FIRMWATCH_LOAD_FIRMWARE    _IOW(FIRMWATCH_IOC_MAGIC, 1, struct firmware_load_req)
#define FIRMWATCH_UNLOAD_FIRMWARE  _IOW(FIRMWATCH_IOC_MAGIC, 2, int)
#define FIRMWATCH_GET_INFO         _IOWR(FIRMWATCH_IOC_MAGIC, 4, struct firmware_info)

struct firmware_load_req {
    char name[256];        // Firmware filename
    size_t size;           // Size (filled by kernel)
    int slot_id;           // Slot ID (-1 for auto-assign)
};

struct firmware_info {
    int slot_id;           // Slot ID
    char name[256];        // Firmware name
    size_t size;           // Size in bytes
    unsigned long load_time; // Load timestamp (jiffies)
    int ref_count;         // Number of active mappings
};
```

#### Memory Mapping

```c
// Open device
int fd = open("/dev/firmwatch", O_RDWR);

// Get firmware info
struct firmware_info info = {.slot_id = 0};
ioctl(fd, FIRMWATCH_GET_INFO, &info);

// Memory map firmware (slot ID as offset)
void *firmware_data = mmap(NULL, info.size, PROT_READ, MAP_SHARED, fd, 0);

// Use firmware data directly
process_firmware(firmware_data, info.size);

// Cleanup
munmap(firmware_data, info.size);
close(fd);
```

## Hot-Reload Example Application

Here's a complete example of an application that uses hot-reloadable firmware:

```c
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <signal.h>

#define FIRMWATCH_IOC_MAGIC 'F'
#define FIRMWATCH_GET_INFO _IOWR(FIRMWATCH_IOC_MAGIC, 4, struct firmware_info)

struct firmware_info {
    int slot_id;
    char name[256];
    size_t size;
    unsigned long load_time;
    int ref_count;
};

static volatile int running = 1;
static void sig_handler(int sig) { running = 0; }

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <slot_id>\n", argv[0]);
        return 1;
    }
    
    int slot_id = atoi(argv[1]);
    int fd = open("/dev/firmwatch", O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }
    
    signal(SIGINT, sig_handler);
    
    struct firmware_info info = {.slot_id = slot_id};
    void *firmware_data = NULL;
    unsigned long last_load_time = 0;
    
    printf("Monitoring firmware slot %d (Ctrl+C to exit)\n", slot_id);
    
    while (running) {
        // Check firmware info
        if (ioctl(fd, FIRMWATCH_GET_INFO, &info) < 0) {
            printf("Slot %d not loaded\n", slot_id);
            sleep(1);
            continue;
        }
        
        // Check if firmware was reloaded
        if (info.load_time != last_load_time) {
            if (firmware_data) {
                munmap(firmware_data, info.size);
                printf("Unmapped old firmware\n");
            }
            
            // Map new firmware
            firmware_data = mmap(NULL, info.size, PROT_READ, MAP_SHARED, fd, slot_id);
            if (firmware_data == MAP_FAILED) {
                perror("mmap");
                break;
            }
            
            last_load_time = info.load_time;
            printf("Mapped new firmware: %s (%zu bytes)\n", info.name, info.size);
            
            // Process firmware (example: print first 32 bytes as hex)
            printf("First 32 bytes: ");
            for (int i = 0; i < 32 && i < info.size; i++) {
                printf("%02x ", ((unsigned char*)firmware_data)[i]);
            }
            printf("\n");
        }
        
        sleep(1);
    }
    
    if (firmware_data) {
        munmap(firmware_data, info.size);
    }
    close(fd);
    
    return 0;
}
```

## Monitoring and Debugging

### Proc Interface

The `/proc/firmwatch` file provides real-time status information:

```bash
cat /proc/firmwatch
```

Output:
```
FirmWatch Status
================

Slot Name                            Size         RefCount Load Time
---- ----                            ----         -------- ---------
0    firmware_v1.bin                 4096         1        4295234567
3    bootloader.bin                  8192         0        4295234890
```

### Kernel Log Messages

Monitor kernel messages for debugging:

```bash
# Follow kernel log
sudo dmesg -f kern -w | grep firmwatch

# Or check recent messages
dmesg | grep firmwatch
```

### Debug Information

Enable verbose logging by rebuilding with debug flags:

```bash
# Add to Makefile CFLAGS
EXTRA_CFLAGS += -DDEBUG

make clean && make module
```

## Error Handling

Common error conditions and their meanings:

- **ENOENT**: Firmware slot not in use
- **EBUSY**: Firmware slot busy (still mapped by applications)
- **EINVAL**: Invalid slot ID or parameters
- **ENOMEM**: Out of memory
- **EFAULT**: Invalid user space pointer
- **ENODEV**: Firmware not found by kernel firmware loader

## Limitations

- Maximum firmware size: 16MB per slot
- Maximum slots: 256 simultaneous firmware blobs
- Memory mapping is read-only
- Requires firmware files to be in standard Linux firmware directories

## Security Considerations

- Device permissions controlled by udev rules
- Only root can load/unload the kernel module
- Firmware loading uses standard kernel security mechanisms
- Memory mappings are read-only to prevent tampering

## Cleanup

```bash
# Unload module
sudo make unload

# Remove installation
sudo make uninstall

# Clean build artifacts
make clean
```

## License

GPL v2 - See source files for full license text.

## Contributing

1. Fork the repository
2. Create feature branch
3. Make changes with appropriate testing
4. Submit pull request

## Troubleshooting

### Module won't load
- Check kernel version compatibility
- Verify kernel headers are installed
- Check dmesg for error messages

### Device not accessible
- Verify udev rules are installed
- Check device permissions: `ls -l /dev/firmwatch`
- Ensure user is in appropriate group

### Firmware won't load
- Check firmware file exists in `/lib/firmware/`
- Verify firmware filename matches request
- Check available memory

### Memory mapping fails
- Verify slot is loaded
- Check process has appropriate permissions
- Ensure firmware size is reasonable
