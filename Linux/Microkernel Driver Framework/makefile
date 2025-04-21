# Makefile for microkernel and sample driver modules

obj-m += microkernel.o sample_driver.o

# Specify source files for each module
microkernel-objs := microkernel.o

# Set the path to the kernel source
KDIR := /lib/modules/$(shell uname -r)/build

all:
	make -C $(KDIR) M=$(PWD) modules

clean:
	make -C $(KDIR) M=$(PWD) clean

install:
	make -C $(KDIR) M=$(PWD) modules_install
	depmod -a
