# Microkernel-like Module Framework

A Linux kernel module framework that enables loading driver modules at runtime, similar to a microkernel architecture.

## Overview

This project implements a modular kernel framework that allows dynamic loading and unloading of driver modules at runtime. The system consists of:

1. **Core Microkernel Module** - Provides the bus infrastructure, device management, and registration system
2. **Sample Driver Module** - Demonstrates how to implement a driver that registers with the microkernel

The framework creates a custom bus type that handles driver and device registration, along with per-instance character devices that provide user-space interfaces to the loaded drivers.

## Features

- Custom bus type implementation (`micro_bus`)
- Driver registration mechanism with standard lifecycle callbacks
- Per-instance character devices with separate namespaces
- Automatic device file creation in `/dev`
- Support for standard file operations (open, read, write, etc.)
- Independent driver operation after registration
- Hot-pluggable driver support

## Requirements

- Linux kernel headers
- Build tools (make, gcc)
- Root access for loading modules

## Building

To build the modules:

```bash
make
```

## Installation

Load the modules in the correct order:

```bash
# First load the core microkernel module
sudo insmod microkernel.ko

# Then load any driver modules (like the sample driver)
sudo insmod sample_driver.ko
```

After loading, the system will create the necessary device files in `/dev` (e.g., `/dev/sample_driver`).

## Usage

### Basic Interaction

Once loaded, you can interact with the sample driver through its character device:

```bash
# Write to the device
echo "Hello from microkernel" > /dev/sample_driver

# Read from the device
cat /dev/sample_driver
```

### Unloading

To unload, remove the modules in reverse order:

```bash
sudo rmmod sample_driver
sudo rmmod microkernel
```

## Project Structure

- `microkernel.h` - Header file with type definitions and API declarations
- `microkernel.c` - Core implementation of the bus, device, and registration system
- `sample_driver.c` - Example driver that registers with the microkernel
- `Makefile` - Build system configuration

## API Reference

### Driver Registration

```c
int micro_register_driver(struct micro_driver *driver);
void micro_unregister_driver(struct micro_driver *driver);
```

### Device Management

```c
struct micro_device *micro_create_device(const char *name, void *data);
void micro_destroy_device(struct micro_device *device);
```

### Driver Structure

```c
struct micro_driver {
    const char *name;
    int (*init)(struct micro_driver *drv, void *data);
    void (*exit)(struct micro_driver *drv);
    int (*open)(struct micro_driver *drv, struct file *filp);
    int (*release)(struct micro_driver *drv, struct file *filp);
    ssize_t (*read)(struct micro_driver *drv, struct file *filp, char __user *buf, size_t count, loff_t *offset);
    ssize_t (*write)(struct micro_driver *drv, struct file *filp, const char __user *buf, size_t count, loff_t *offset);
    long (*ioctl)(struct micro_driver *drv, struct file *filp, unsigned int cmd, unsigned long arg);
    
    struct device_driver driver;  // Embedded driver structure
    void *private_data;           // Driver's private data
};
```

## Writing New Drivers

To create a new driver for the microkernel:

1. Include the microkernel header: `#include "microkernel.h"`
2. Define a `micro_driver` structure with your callbacks
3. Register the driver with `micro_register_driver()`
4. Create device(s) with `micro_create_device()`

See `sample_driver.c` for a complete example.

## Design Patterns

The framework follows the Linux kernel's device model with:

- Device-driver binding through bus matching
- Clear separation between bus, driver, and device layers
- Standard lifecycle management (probe, remove, init, exit)
- Character device operations delegation

## Notes for Kernel Developers

- The framework uses standard kernel APIs like `bus_type`, `device_driver`, and `device_create()`
- All memory allocations are properly handled with corresponding free operations
- Proper locking mechanism is implemented to ensure thread safety
- The module follows standard kernel coding practices
