# Bufstat - Kernel Buffer Statistics Driver

A Linux kernel module that creates a character device `/dev/bufstat` to monitor and report kernel buffer utilization across various subsystems.

## Overview

The `bufstat` driver provides a simple interface to monitor kernel buffer usage across multiple subsystems including:

- Network buffers (socket buffers, per-device statistics)
- Block I/O buffers (page cache, dirty pages)
- Memory management buffers (slab allocation, page tables)
- Overall memory utilization

This driver is useful for system administrators, developers, and performance analysts who need visibility into kernel memory usage patterns without having to manually parse multiple /proc entries.

## Features

- **Real-time statistics**: Fresh data generated on each read
- **Cross-subsystem visibility**: Network, block I/O, and memory management in one view
- **Simple interface**: Read from `/dev/bufstat` as a regular file
- **Minimal overhead**: Optimized to collect statistics with low performance impact
- **Configurable**: Runtime debug mode toggle

## Installation

### Prerequisites

- Linux kernel headers for your running kernel
- Build tools (make, gcc, etc.)

### Building the module

1. Clone this repository or download the source code
2. Navigate to the source directory
3. Run make:

```bash
make
```

### Loading the module

To load the module:

```bash
sudo insmod bufstat.ko
```

Optionally, you can enable debug mode when loading:

```bash
sudo insmod bufstat.ko debug_mode=1
```

### Setting up device permissions

After loading the module, the device will be created as `/dev/bufstat` with root permissions. To make it readable by non-root users:

```bash
sudo chmod 0444 /dev/bufstat
```

For persistent permission setting, create a udev rule in `/etc/udev/rules.d/99-bufstat.rules`:

```
KERNEL=="bufstat", MODE="0444"
```

### Unloading the module

```bash
sudo rmmod bufstat
```

## Usage

### Reading statistics

Simply read from the device file:

```bash
cat /dev/bufstat
```

Example output:

```
Kernel Buffer Statistics Report
Generated: 1716294825.123

=== Network Buffer Statistics ===
Socket buffer allocation: 384

Per-device buffer statistics:
  eth0: rx_buffers=12984 tx_buffers=5643
  lo: rx_buffers=245 tx_buffers=245

=== Block Buffer Statistics ===
Buffers: 342516 kB
Cached: 2145280 kB
Dirty pages: 352 kB
Writeback pages: 0 kB

=== Memory Management Statistics ===
Slab memory: 163840 kB
  Reclaimable: 91248 kB
  Unreclaimable: 72592 kB
PageTables: 23148 kB

=== Overall Buffer Usage Summary ===
Total memory: 8192000 kB
Free memory: 4598456 kB
Available memory: 6410532 kB
```

### Monitoring changes over time

To monitor how buffer usage changes over time:

```bash
watch -n 1 cat /dev/bufstat
```

### Control commands

The module supports simple control commands via write operations:

```bash
# Enable debug mode
echo "debug" > /dev/bufstat

# Disable debug mode
echo "nodebug" > /dev/bufstat
```

Debug messages are logged to the kernel log and can be viewed with `dmesg`.

## Troubleshooting

### Module fails to load

Check kernel logs for specific error messages:

```bash
dmesg | tail
```

Common issues:
- Missing kernel headers
- Version mismatch between kernel and headers
- Insufficient permissions

### "Device or resource busy" error

Only one process can open the device at a time. Check if another process is already using it:

```bash
lsof /dev/bufstat
```

### Getting no output

If reading from the device returns nothing, check:
1. Is the module loaded? Check with `lsmod | grep bufstat`
2. Does the device exist? Verify with `ls -l /dev/bufstat`
3. Do you have read permissions on the device?

## Performance Considerations

The `bufstat` module has been designed to have minimal impact on system performance. However, frequent reads from the device will cause the kernel to collect statistics each time, which may have a small impact on very heavily loaded systems.

For performance-critical environments, consider:
- Reading less frequently
- Using the module only during debugging or performance analysis

## Development

### Module Parameters

- `debug_mode`: Enable (1) or disable (0) debug messages in kernel log

### Code Structure

- `bufstat.c`: Main module implementation
- `Makefile`: Build configuration

### Adding New Metrics

To add monitoring for additional kernel subsystems:

1. Create a new collection function similar to `get_network_buffer_stats()`
2. Add a call to your function in `generate_buffer_stats()`
3. Make sure to check available buffer space before writing

## License

This module is released under the GPL v2 license.

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

## Authors

This module was created to provide kernel developers with valuable insights into buffer utilization across the kernel's subsystems.
