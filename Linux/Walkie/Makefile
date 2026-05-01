obj-m += walkie.o

KDIR  ?= /lib/modules/$(shell uname -r)/build
PWD   := $(shell pwd)

# ── build ──────────────────────────────────────────────────────────────────
all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

# ── clean ──────────────────────────────────────────────────────────────────
clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -f test_walkie

# ── test binary ────────────────────────────────────────────────────────────
test_walkie: test_walkie.c
	$(CC) -Wall -Wextra -o $@ $<

# ── load / unload helpers ──────────────────────────────────────────────────
load: walkie.ko
	sudo insmod walkie.ko
	@echo "Module loaded. Device: $$(ls -l /dev/walkie)"

unload:
	sudo rmmod walkie
	@echo "Module unloaded."

reload: unload load

# ── run the test suite (loads module automatically if needed) ──────────────
test: test_walkie walkie.ko
	@if ! lsmod | grep -q '^walkie'; then \
		sudo insmod walkie.ko; \
		echo "walkie module loaded."; \
	fi
	sudo ./test_walkie

.PHONY: all clean load unload reload test
