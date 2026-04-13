CXX      = arm-linux-gnueabihf-g++
SYSROOT  = $(HOME)/embedded-linux/buildroot-2024.02/output/host/arm-buildroot-linux-gnueabihf/sysroot
CXXFLAGS = -std=c++17 -O2 -Wall -g --sysroot=$(SYSROOT)
LDFLAGS  = -lboost_system -lpthread --sysroot=$(SYSROOT)
TARGET   = chess_server

$(TARGET): server.cpp game_state.hpp
	$(CXX) $(CXXFLAGS) server.cpp -o $(TARGET) $(LDFLAGS)
	@echo "[+] ARM binary ready: $(TARGET)"

clean:
	rm -f $(TARGET) $(TARGET)_native

# Native x86 build for quick testing without QEMU
native:
	g++ -std=c++17 -O2 -Wall -g server.cpp -o $(TARGET)_native -lboost_system -lpthread
	@echo "[+] Native binary ready: $(TARGET)_native"

inject: $(TARGET)
	sudo mount -o loop $(HOME)/embedded-linux/buildroot-2024.02/output/images/rootfs.ext2 /mnt/rootfs
	sudo cp $(TARGET) /mnt/rootfs/root/
	sudo umount /mnt/rootfs
	@echo "[+] Injected into rootfs"
