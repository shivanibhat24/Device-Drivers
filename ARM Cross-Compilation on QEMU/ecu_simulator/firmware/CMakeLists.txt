cmake_minimum_required(VERSION 3.22)

# ── Toolchain must be set before project() ────
# Set via: cmake -DCMAKE_TOOLCHAIN_FILE=../toolchain-riscv.cmake ..
project(ecu_firmware CXX C ASM)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD   11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ─────────────────────────────────────────────
#  FreeRTOS source
#  Assumes FreeRTOS-Kernel is a subdirectory or
#  fetched via FetchContent below.
# ─────────────────────────────────────────────
include(FetchContent)
FetchContent_Declare(
    freertos_kernel
    GIT_REPOSITORY https://github.com/FreeRTOS/FreeRTOS-Kernel.git
    GIT_TAG        V11.1.0
    GIT_SHALLOW    TRUE
)
# Tell FreeRTOS which heap implementation to use
set(FREERTOS_HEAP "4" CACHE STRING "" FORCE)
# Tell it we're using the RISC-V port
set(FREERTOS_PORT "GCC_RISC_V" CACHE STRING "" FORCE)
FetchContent_MakeAvailable(freertos_kernel)

# ─────────────────────────────────────────────
#  ECU Firmware target
# ─────────────────────────────────────────────
add_executable(ecu_firmware
    src/startup.S
    src/main.cpp
    src/ecu_state.cpp
    hal/hal_uart.cpp
)

target_include_directories(ecu_firmware PRIVATE
    include
    hal
    tasks
    freertos_config
    ${freertos_kernel_SOURCE_DIR}/include
    ${freertos_kernel_SOURCE_DIR}/portable/GCC/RISC-V
)

target_link_libraries(ecu_firmware PRIVATE
    freertos_kernel
)

# ── RISC-V bare-metal flags ────────────────────
target_compile_options(ecu_firmware PRIVATE
    -march=rv64imac
    -mabi=lp64
    -mcmodel=medany
    -ffreestanding
    -fno-exceptions
    -fno-rtti
    -Wall
    -Wextra
    -Og -g3                      # Debug build — optimise for size
)

target_link_options(ecu_firmware PRIVATE
    -march=rv64imac
    -mabi=lp64
    -mcmodel=medany
    -nostartfiles
    -T ${CMAKE_SOURCE_DIR}/linker/sifive_u.ld
    -Wl,-Map=${CMAKE_BINARY_DIR}/ecu_firmware.map
    -Wl,--gc-sections
)

# ── ELF → binary ──────────────────────────────
add_custom_command(TARGET ecu_firmware POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary
        $<TARGET_FILE:ecu_firmware>
        ${CMAKE_BINARY_DIR}/ecu_firmware.bin
    COMMENT "Converting ELF to raw binary"
)

# ── QEMU launch helper target ─────────────────
add_custom_target(run_qemu
    COMMAND qemu-system-riscv64
        -machine sifive_u
        -nographic
        -bios none
        -kernel ${CMAKE_BINARY_DIR}/ecu_firmware.elf
        -serial pty                              # UART0 → PTY (CAN frames)
        -serial tcp::5001,server,nowait          # UART1 → TCP :5001 (control)
    DEPENDS ecu_firmware
    COMMENT "Launching ECU firmware in QEMU"
)
