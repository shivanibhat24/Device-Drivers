# Makefile for edu_driver — out-of-tree kernel module
#
# Usage:
#   make                      # build with defaults
#   make KDIR=/path/to/linux  # specify kernel source
#   make clean                # remove build artifacts

KDIR          ?= $(HOME)/linux
ARCH          ?= arm64
CROSS_COMPILE ?= aarch64-linux-gnu-

obj-m := edu_driver.o

all:
	$(MAKE) -C $(KDIR) M=$(PWD) \
	  ARCH=$(ARCH) \
	  CROSS_COMPILE=$(CROSS_COMPILE) \
	  modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) \
	  ARCH=$(ARCH) \
	  CROSS_COMPILE=$(CROSS_COMPILE) \
	  clean

.PHONY: all clean
