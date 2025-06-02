obj-m := cpu_affinity_mapper.o

KERNEL_DIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

all:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) clean

install:
	sudo insmod cpu_affinity_mapper.ko

uninstall:
	sudo rmmod cpu_affinity_mapper

reload: uninstall install

test:
	gcc -o test_cam test_cam.c
	sudo ./test_cam

.PHONY: all clean install uninstall reload test
