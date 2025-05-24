# FirmWatch Makefile
# Builds both kernel module and userspace utility

# Kernel module
obj-m := firmwatch.o

# Kernel build directory (adjust as needed)
KERNEL_DIR ?= /lib/modules/$(shell uname -r)/build

# Userspace utility
USERSPACE_TARGET := firmwatch-util
USERSPACE_SRC := firmwatch-util.c

# Compiler flags for userspace
CC := gcc
CFLAGS := -Wall -Wextra -O2 -std=c99

.PHONY: all clean install uninstall module userspace

all: module userspace

# Build kernel module
module:
	@echo "Building kernel module..."
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) modules

# Build userspace utility
userspace: $(USERSPACE_TARGET)

$(USERSPACE_TARGET): $(USERSPACE_SRC)
	@echo "Building userspace utility..."
	$(CC) $(CFLAGS) -o $@ $<

# Clean build artifacts
clean:
	@echo "Cleaning build artifacts..."
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) clean
	rm -f $(USERSPACE_TARGET)
	rm -f *.o *.ko *.mod.c *.mod *.symvers *.order
	rm -f .*.cmd

# Install module and utility
install: all
	@echo "Installing FirmWatch..."
	# Install kernel module
	sudo cp firmwatch.ko /lib/modules/$(shell uname -r)/extra/
	sudo depmod -a
	# Install userspace utility
	sudo cp $(USERSPACE_TARGET) /usr/local/bin/
	sudo chmod +x /usr/local/bin/$(USERSPACE_TARGET)
	# Create udev rule for device permissions
	echo 'KERNEL=="firmwatch", MODE="0666"' | sudo tee /etc/udev/rules.d/99-firmwatch.rules
	sudo udevadm control --reload-rules
	@echo "Installation complete!"
	@echo "Load the module with: sudo modprobe firmwatch"

# Uninstall module and utility
uninstall:
	@echo "Uninstalling FirmWatch..."
	# Remove kernel module
	sudo rm -f /lib/modules/$(shell uname -r)/extra/firmwatch.ko
	sudo depmod -a
	# Remove userspace utility
	sudo rm -f /usr/local/bin/$(USERSPACE_TARGET)
	# Remove udev rule
	sudo rm -f /etc/udev/rules.d/99-firmwatch.rules
	sudo udevadm control --reload-rules
	@echo "Uninstallation complete!"

# Load the kernel module
load:
	@echo "Loading FirmWatch module..."
	sudo modprobe firmwatch
	@echo "Module loaded. Device should be available at /dev/firmwatch"

# Unload the kernel module
unload:
	@echo "Unloading FirmWatch module..."
	sudo rmmod firmwatch || true
	@echo "Module unloaded."

# Show module info
info:
	@echo "FirmWatch module information:"
	modinfo firmwatch.ko 2>/dev/null || echo "Module not built yet"
	@echo ""
	@echo "Loaded modules:"
	lsmod | grep firmwatch || echo "Module not loaded"
	@echo ""
	@echo "Device status:"
	ls -l /dev/firmwatch 2>/dev/null || echo "Device not available"
	@echo ""
	@echo "Proc status:"
	cat /proc/firmwatch 2>/dev/null || echo "Proc entry not available"

# Development helpers
dev-clean: unload clean

dev-reload: unload clean all load

# Test firmware loading (requires test firmware files)
test:
	@echo "Running basic tests..."
	@if [ ! -c /dev/firmwatch ]; then \
		echo "Error: /dev/firmwatch not found. Load the module first."; \
		exit 1; \
	fi
	@echo "Testing firmware list (should be empty):"
	./$(USERSPACE_TARGET) list
	@echo ""
	@echo "Testing device info:"
	ls -l /dev/firmwatch
	@echo ""
	@echo "Testing proc interface:"
	cat /proc/firmwatch 2>/dev/null || echo "Proc entry not available"

# Create test firmware files
create-test-firmware:
	@echo "Creating test firmware files..."
	@mkdir -p test_firmware
	@dd if=/dev/urandom of=test_firmware/test1.bin bs=1024 count=4 2>/dev/null
	@dd if=/dev/urandom of=test_firmware/test2.bin bs=1024 count=8 2>/dev/null
	@echo "Test firmware files created in test_firmware/"

# Help target
help:
	@echo "FirmWatch Build System"
	@echo "====================="
	@echo ""
	@echo "Targets:"
	@echo "  all              - Build both kernel module and userspace utility"
	@echo "  module           - Build kernel module only"
	@echo "  userspace        - Build userspace utility only"
	@echo "  clean            - Clean build artifacts"
	@echo "  install          - Install module and utility system-wide"
	@echo "  uninstall        - Remove installed files"
	@echo "  load             - Load the kernel module"
	@echo "  unload           - Unload the kernel module"
	@echo "  info             - Show module and device status"
	@echo "  dev-clean        - Unload module and clean (for development)"
	@echo "  dev-reload       - Full rebuild and reload (for development)"
	@echo "  test             - Run basic functionality tests"
	@echo "  create-test-firmware - Create test firmware files"
	@echo "  help             - Show this help message"
	@echo ""
	@echo "Environment Variables:"
	@echo "  KERNEL_DIR       - Kernel build directory (default: current kernel)"
	@echo ""
	@echo "Example Usage:"
	@echo "  make all         # Build everything"
	@echo "  make install     # Install system-wide"
	@echo "  make load        # Load the module"
	@echo "  make test        # Test basic functionality"
