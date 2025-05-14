# USB Watchdog Driver - Build and Usage Instructions

## Overview

The USB Watchdog Driver (`/dev/usbwatchdog`) is a Linux kernel module designed to monitor and control USB device connections. It maintains a whitelist of approved USB devices and logs all unauthorized device connections, providing an essential security measure for systems requiring USB device control.

## Key Features

- Creates a `/dev/usbwatchdog` device for configuration and status
- Maintains a whitelist of approved USB devices based on VID/PID or device class
- Logs all unauthorized USB device connections with configurable verbosity
- Provides ioctl interface for whitelist management
- Readable status output showing current whitelist and settings

## Build Instructions

### Prerequisites

- Linux kernel headers for your kernel version
- Build tools: gcc, make

### Compilation

1. Create a Makefile in the same directory as the usbwatchdog.c file:

```makefile
obj-m += usbwatchdog.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
```

2. Build the module:

```bash
make
```

3. Install the module:

```bash
sudo insmod usbwatchdog.ko
```

4. Verify installation:

```bash
lsmod | grep usbwatchdog
dmesg | grep USBWATCHDOG
ls -l /dev/usbwatchdog
```

## Usage Instructions

### Basic Usage

1. Read current status:

```bash
cat /dev/usbwatchdog
```

2. Create a simple management tool (save as `usbwatchdog_ctl.c`):

```c
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>

/* Same structure as in the driver */
struct whitelist_device {
    unsigned short vendor_id;
    unsigned short product_id;
    unsigned short device_class;
    char manufacturer[64];
    char product[64];
    struct list_head {
        void *next;
        void *prev;
    } list;
};

/* Same IOCTLs as in the driver */
#define USBWATCHDOG_IOC_MAGIC 'u'
#define USBWATCHDOG_ADD_DEVICE _IOW(USBWATCHDOG_IOC_MAGIC, 1, struct whitelist_device)
#define USBWATCHDOG_REMOVE_DEVICE _IOW(USBWATCHDOG_IOC_MAGIC, 2, struct whitelist_device)
#define USBWATCHDOG_CLEAR_WHITELIST _IO(USBWATCHDOG_IOC_MAGIC, 3)
#define USBWATCHDOG_SET_LOG_LEVEL _IOW(USBWATCHDOG_IOC_MAGIC, 5, int)

void print_usage(void) {
    printf("Usage: usbwatchdog_ctl [COMMAND] [OPTIONS]\n\n");
    printf("Commands:\n");
    printf("  add VID PID [CLASS] [MANUFACTURER] [PRODUCT]  Add device to whitelist\n");
    printf("  remove VID PID                                Remove device from whitelist\n");
    printf("  clear                                         Clear entire whitelist\n");
    printf("  loglevel LEVEL                                Set log level (0-2)\n");
    printf("  status                                        Show current status\n\n");
    printf("Examples:\n");
    printf("  usbwatchdog_ctl add 045e 0040                 Add Microsoft mouse\n");
    printf("  usbwatchdog_ctl add 046d 0c25 \"Logitech\" \"Gaming Mouse\" Add with names\n");
    printf("  usbwatchdog_ctl loglevel 2                    Set verbose logging\n");
}

int main(int argc, char *argv[]) {
    int fd, ret = 0;
    struct whitelist_device dev;
    int log_level;
    
    if (argc < 2) {
        print_usage();
        return 1;
    }
    
    fd = open("/dev/usbwatchdog", O_RDWR);
    if (fd < 0) {
        perror("Failed to open /dev/usbwatchdog");
        return 1;
    }
    
    if (strcmp(argv[1], "add") == 0) {
        if (argc < 4) {
            printf("Error: VID and PID are required for add command\n");
            ret = 1;
            goto exit;
        }
        
        memset(&dev, 0, sizeof(dev));
        dev.vendor_id = (unsigned short)strtol(argv[2], NULL, 16);
        dev.product_id = (unsigned short)strtol(argv[3], NULL, 16);
        
        if (argc > 4) {
            dev.device_class = (unsigned short)strtol(argv[4], NULL, 16);
        }
        
        if (argc > 5) {
            strncpy(dev.manufacturer, argv[5], 63);
        }
        
        if (argc > 6) {
            strncpy(dev.product, argv[6], 63);
        }
        
        if (ioctl(fd, USBWATCHDOG_ADD_DEVICE, &dev) < 0) {
            perror("Failed to add device to whitelist");
            ret = 1;
            goto exit;
        }
        
        printf("Device %04x:%04x added to whitelist\n", dev.vendor_id, dev.product_id);
    } 
    else if (strcmp(argv[1], "remove") == 0) {
        if (argc < 4) {
            printf("Error: VID and PID are required for remove command\n");
            ret = 1;
            goto exit;
        }
        
        memset(&dev, 0, sizeof(dev));
        dev.vendor_id = (unsigned short)strtol(argv[2], NULL, 16);
        dev.product_id = (unsigned short)strtol(argv[3], NULL, 16);
        
        if (ioctl(fd, USBWATCHDOG_REMOVE_DEVICE, &dev) < 0) {
            perror("Failed to remove device from whitelist");
            ret = 1;
            goto exit;
        }
        
        printf("Device %04x:%04x removed from whitelist\n", dev.vendor_id, dev.product_id);
    } 
    else if (strcmp(argv[1], "clear") == 0) {
        if (ioctl(fd, USBWATCHDOG_CLEAR_WHITELIST) < 0) {
            perror("Failed to clear whitelist");
            ret = 1;
            goto exit;
        }
        
        printf("Whitelist cleared\n");
    } 
    else if (strcmp(argv[1], "loglevel") == 0) {
        if (argc < 3) {
            printf("Error: Log level value is required\n");
            ret = 1;
            goto exit;
        }
        
        log_level = atoi(argv[2]);
        if (log_level < 0 || log_level > 2) {
            printf("Error: Log level must be between 0 and 2\n");
            ret = 1;
            goto exit;
        }
        
        if (ioctl(fd, USBWATCHDOG_SET_LOG_LEVEL, &log_level) < 0) {
            perror("Failed to set log level");
            ret = 1;
            goto exit;
        }
        
        printf("Log level set to %d\n", log_level);
    } 
    else if (strcmp(argv[1], "status") == 0) {
        char buffer[4096];
        int bytes_read;
        
        bytes_read = read(fd, buffer, sizeof(buffer) - 1);
        if (bytes_read < 0) {
            perror("Failed to read status");
            ret = 1;
            goto exit;
        }
        
        buffer[bytes_read] = '\0';
        printf("%s\n", buffer);
    } 
    else {
        printf("Unknown command: %s\n", argv[1]);
        print_usage();
        ret = 1;
    }
    
exit:
    close(fd);
    return ret;
}
```

3. Compile the management tool:

```bash
gcc -o usbwatchdog_ctl usbwatchdog_ctl.c
```

### Whitelist Management

Add your authorized USB devices to the whitelist:

```bash
# Add a device by vendor/product ID (e.g., for a specific USB flash drive)
sudo ./usbwatchdog_ctl add 0781 5567

# Add a device with manufacturer and product description
sudo ./usbwatchdog_ctl add 046d c52b "Logitech" "Unifying Receiver"

# Add a device class (e.g., 03 for Human Interface Devices)
sudo ./usbwatchdog_ctl add 0000 0000 03

# Remove a device from whitelist
sudo ./usbwatchdog_ctl remove 0781 5567

# Clear the entire whitelist
sudo ./usbwatchdog_ctl clear
```

### Logging Configuration

Set the log level to control verbosity:

```bash
# Minimal logging (only log unauthorized devices)
sudo ./usbwatchdog_ctl loglevel 0

# Normal logging (unauthorized devices with basic info)
sudo ./usbwatchdog_ctl loglevel 1

# Verbose logging (all devices with detailed info)
sudo ./usbwatchdog_ctl loglevel 2
```

### Monitoring

Monitor USB device connections:

```bash
# View kernel logs for USB device connections
sudo dmesg -w | grep USBWATCHDOG

# Check current whitelist and settings
cat /dev/usbwatchdog
```

## Security Considerations

- This driver is intended as a monitoring and logging tool, not as a complete USB security solution
- It does not prevent devices from being recognized by the system or loaded by the kernel
- For comprehensive USB security, combine with other measures like USB port disabling, USBGuard, etc.
- Maintain physical security of the system for maximum effectiveness

## Troubleshooting

1. If module doesn't load:
   - Check kernel logs: `dmesg | tail`
   - Verify kernel headers match running kernel
   - Check for any dependency issues

2. If device not created:
   - Verify module is loaded: `lsmod | grep usbwatchdog`
   - Check kernel logs for any device creation errors

3. If unauthorized devices aren't being logged:
   - Verify log level is set appropriately
   - Check kernel log configuration
   - Ensure USB notifications are working
