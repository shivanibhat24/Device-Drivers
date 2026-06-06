# Automotive ECU Simulator

A full-stack embedded systems simulator: an automotive ECU running on a QEMU-emulated RISC-V processor inside WSL, controlled and monitored through a Qt 6 GUI on Windows.

```
┌──────────────────────────────────────────────────────────────┐
│  Windows                                                     │
│  ┌────────────────────────────────────────────────────────┐  │
│  │  Qt 6 GUI                                              │  │
│  │  ┌──────────┐ ┌───────────────┐ ┌────────┐ ┌───────┐  │  │
│  │  │Dashboard │ │Fault Injector │ │  CAN   │ │  DTC  │  │  │
│  │  │RPM Gauges│ │Throttle slider│ │Monitor │ │Viewer │  │  │
│  │  │Temp/Fuel │ │Overheat/Disc/ │ │Frame   │ │P0217  │  │  │
│  │  │Battery   │ │Voltage drop   │ │table   │ │P0562  │  │  │
│  │  └──────────┘ └───────────────┘ └────────┘ └───────┘  │  │
│  └────────┬────────────────────────────┬───────────────────┘  │
│           │ TCP :5001                  │ QSerialPort / COM    │
│           │ ControlCmd bytes           │ CAN frames (UART0)   │
└───────────┼────────────────────────────┼──────────────────────┘
            │  WSL 2                     │
            │  ┌─────────────────────────┼───────────────────┐
            │  │  QEMU sifive_u          │                   │
            │  │  UART1 ─────────────────┘                   │
            │  │  UART0 ──→ PTY ──→ socat ──→ TCP :5002      │
            │  │                                              │
            │  │  FreeRTOS on RISC-V (rv64imac)              │
            │  │  T1 Engine   T2 Sensors   T3 CAN TX          │
            │  │  T4 Diag     T5 Watchdog  UART RX            │
            │  └──────────────────────────────────────────────┘
```

---

## Skills Demonstrated

| Skill | Implementation |
|---|---|
| **FreeRTOS** | 5 tasks, queues, mutex-protected shared state, software watchdog |
| **CAN Bus** | Framed wire protocol over UART, 7 message IDs, OBD-II DTC range |
| **State machines** | Engine OFF → CRANKING → RUNNING → FAULT in `task_engine.hpp` |
| **Fault handling** | 3 injectable faults, DTC logging, watchdog hang detection |
| **Bare-metal HAL** | SiFive UART register-level C++ driver for QEMU sifive_u |
| **Linker script** | Custom `sifive_u.ld` for RISC-V bare-metal memory layout |
| **Startup code** | RISC-V assembly startup: stack init, BSS zero, C++ ctors, main |
| **Qt 6** | Signals/slots, custom `QPainter` arc gauges, QSerialPort, QTcpSocket |
| **CMake** | Cross-compilation toolchain file, FetchContent for FreeRTOS-Kernel |
| **QEMU** | sifive_u machine, dual UART bridging (PTY + TCP) |

---

## Repository Structure

```
ecu_simulator/
├── .gitignore
├── README.md                       ← You are here
│
├── firmware/                       RISC-V firmware (C++17 + FreeRTOS)
│   ├── CMakeLists.txt              Fetches FreeRTOS, cross-compiles
│   ├── toolchain-riscv.cmake       GCC RISC-V cross-compiler config
│   │
│   ├── include/
│   │   ├── ecu_protocol.hpp        ★ SHARED with Qt GUI — CAN IDs, wire
│   │   │                             format, ControlCmd enum, FaultCode enum
│   │   └── ecu_state.hpp           FreeRTOS queue handles + ECUState struct
│   │
│   ├── hal/
│   │   ├── hal_uart.hpp            SiFive UART register map + C++ driver
│   │   └── hal_uart.cpp            UART0/UART1 global instances
│   │
│   ├── tasks/
│   │   ├── task_engine.hpp         T1: RPM state machine (50ms, pri 3)
│   │   ├── task_sensors.hpp        T2: Sensor sim + fault injection (100ms, pri 3)
│   │   ├── task_can_tx.hpp         T3: CAN frame builder + UART TX (100ms, pri 2)
│   │   ├── task_diag.hpp           T4: DTC log + diagnostic frames (event, pri 1)
│   │   └── task_watchdog.hpp       T5: Software watchdog (500ms, pri 4)
│   │
│   ├── freertos_config/
│   │   └── FreeRTOSConfig.h        FreeRTOS tuning for sifive_u
│   │
│   ├── linker/
│   │   └── sifive_u.ld             Linker script: DRAM layout, heap, stack
│   │
│   └── src/
│       ├── startup.S               RISC-V boot: stack, BSS, ctors, main
│       ├── main.cpp                Task creation + FreeRTOS scheduler start
│       └── ecu_state.cpp           Queue/mutex initialisation
│
├── qt_gui/                         Qt 6 Windows GUI
│   ├── CMakeLists.txt
│   │
│   ├── include/
│   │   ├── ConnectionManager.hpp   TCP control + QSerialPort CAN stream
│   │   ├── CANParser.hpp           Raw CANFrame → typed Qt signals
│   │   ├── ECUDashboard.hpp        Arc gauge + bar gauge widgets
│   │   ├── FaultInjector.hpp       Throttle slider + fault buttons
│   │   ├── CANMonitor.hpp          Scrolling CAN frame table
│   │   ├── DTCViewer.hpp           DTC log with code descriptions
│   │   └── MainWindow.hpp          Top-level window
│   │
│   ├── src/
│   │   ├── main.cpp                Qt entry, dark palette, MainWindow
│   │   ├── ConnectionManager.cpp   Frame parser state machine, TCP/serial
│   │   ├── CANParser.cpp           Decodes all 7 CAN IDs
│   │   ├── ECUDashboard.cpp        Custom QPainter arc gauge, bar gauges
│   │   ├── FaultInjector.cpp       Styled buttons, throttle slider
│   │   ├── CANMonitor.cpp          QTableWidget with colour-coded rows
│   │   ├── DTCViewer.cpp           DTC table with P-code descriptions
│   │   └── MainWindow.cpp          All signal/slot wiring in one place
│   │
│   └── resources/
│       └── resources.qrc           Qt resource file (extend for icons)
│
└── scripts/
    ├── build_firmware.sh           One-shot firmware build in WSL
    ├── launch_qemu.sh              Start QEMU + socat PTY bridge
    └── create_com_bridge.sh        Bridge WSL PTY → Windows COM port
```

---

## Prerequisites

### WSL 2 (Ubuntu 22.04+)

```bash
# RISC-V GCC cross-compiler
sudo apt install gcc-riscv64-unknown-elf g++-riscv64-unknown-elf binutils-riscv64-unknown-elf

# QEMU with RISC-V target
sudo apt install qemu-system-misc

# Build tools
sudo apt install cmake ninja-build

# PTY bridging
sudo apt install socat
```

### Windows

| Tool | Source | Notes |
|---|---|---|
| **Qt 6.5+** | [qt.io](https://www.qt.io/download) | Select: MSVC 2022 64-bit + Qt Serial Port |
| **CMake 3.22+** | [cmake.org](https://cmake.org/download/) | Or via Visual Studio installer |
| **MSVC 2022** | Visual Studio 2022 (Build Tools) | C++ desktop workload |
| **npiperelay** | [GitHub](https://github.com/jstarks/npiperelay/releases) | Optional — for COM port bridging |

---

## Build

### Firmware (in WSL)

```bash
chmod +x scripts/build_firmware.sh
./scripts/build_firmware.sh
```

Or manually:

```bash
cd firmware
mkdir build && cd build
cmake \
  -DCMAKE_TOOLCHAIN_FILE=../toolchain-riscv.cmake \
  -DCMAKE_BUILD_TYPE=Debug \
  -G Ninja \
  ..
ninja -j$(nproc)
```

**Outputs:** `firmware/build/ecu_firmware` (ELF), `ecu_firmware.bin` (raw binary)

### Qt GUI (on Windows, in PowerShell)

```powershell
cd qt_gui
mkdir build; cd build
cmake `
  -DCMAKE_PREFIX_PATH="C:\Qt\6.5.0\msvc2022_64" `
  -G "Visual Studio 17 2022" `
  ..
cmake --build . --config Release
```

Or open `qt_gui/CMakeLists.txt` in Qt Creator (recommended).

---

## Running

### Step 1 — Start QEMU (WSL)

```bash
chmod +x scripts/launch_qemu.sh
./scripts/launch_qemu.sh firmware/build/ecu_firmware
```

Expected output:

```
[LAUNCH] Starting QEMU...
[LAUNCH] QEMU PID: 12345
[LAUNCH] UART0 PTY: /dev/pts/3
[LAUNCH] Bridging /dev/pts/3 → TCP :5002 with socat...

════════════════════════════════════════════
  ECU Simulator running in QEMU
════════════════════════════════════════════
  Control (TCP):  172.24.1.1:5001
  CAN stream:     172.24.1.1:5002  (socat bridge)
  UART0 PTY:      /dev/pts/3
════════════════════════════════════════════
```

The firmware boot log appears on this terminal:

```
[ECU] Booting...
[ECU] Queues ready
[ECU] Scheduler starting
[DIAG] DTCs active=0 total=0
```

### Step 2 — (Optional) Create Windows COM port bridge

If you prefer `QSerialPort` over raw TCP for the CAN stream:

```bash
# Place npiperelay.exe at /mnt/c/tools/npiperelay.exe first
./scripts/create_com_bridge.sh /dev/pts/3
# Qt then connects to: \\.\pipe\ecu_can
```

### Step 3 — Start Qt GUI (Windows)

1. Run `ecu_gui.exe`
2. Click **⚡ Connect to QEMU** in the toolbar
3. Enter:
   - **Control host:** WSL IP from the launch script (e.g. `172.24.1.1`)
   - **Control port:** `5001`
   - **CAN serial port:** `\\.\pipe\ecu_can` or the COM port from Device Manager
4. Click **OK**

The dashboard comes alive immediately as CAN frames arrive.

---

## Communication Protocol

### CAN frames — UART0 (firmware → GUI), 13 bytes fixed

```
┌──────┬──────────┬─────┬──────────────────────────┬──────┐
│ 0xAA │ ID hi:lo │ LEN │     DATA (8 bytes)        │ 0x55 │
│  1B  │    2B    │ 1B  │  (unused bytes = 0x00)    │  1B  │
└──────┴──────────┴─────┴──────────────────────────┴──────┘
Total: 13 bytes
```

| CAN ID | Signal | Encoding | Rate |
|---|---|---|---|
| `0x100` | RPM | uint16 BE, raw value | 10 Hz |
| `0x101` | Throttle | uint8, 0–100 | 10 Hz |
| `0x200` | Coolant temp | int16 BE, value × 10 (855 = 85.5°C) | 2 Hz |
| `0x201` | Fuel level | uint8 0–100; **0xFF = sensor disconnected** | 1 Hz |
| `0x202` | Battery | uint16 BE, millivolts (12600 = 12.6 V) | 1 Hz |
| `0x7E0` | Fault + state | data[0]=fault bitmask, data[1]=engine_state | On change |
| `0x7E8` | DTC | data[0:1]=P-code uint16 BE, data[2]=occurrence count | On fault |

**Fault bitmask (0x7E0 data[0]):**

| Bit | Fault | DTC |
|---|---|---|
| 0 | Coolant over-temperature | P0217 |
| 1 | Sensor disconnected | P0197 |
| 2 | System voltage low | P0562 |
| 3 | Watchdog reset | U0100 |

**Engine state (0x7E0 data[1]):**

| Value | State |
|---|---|
| 0 | OFF |
| 1 | CRANKING |
| 2 | RUNNING |
| 3 | FAULT |

### Control commands — TCP :5001 (GUI → firmware)

| Byte | Command | Additional bytes |
|---|---|---|
| `0x01` | Set throttle | 1 byte (0–100%) |
| `0x10` | Inject overheat | none |
| `0x11` | Inject sensor disconnect | none |
| `0x12` | Inject voltage drop | none |
| `0x20` | Clear all faults | none |
| `0x30` | Set RPM target | 2 bytes uint16 BE |
| `0xFF` | Ping (keepalive, sent every 2 s) | none |

---

## FreeRTOS Architecture

### Task table

| Task | File | Priority | Period | Stack |
|---|---|---|---|---|
| **Watchdog** | `task_watchdog.hpp` | 4 (highest) | 500 ms | 256 words |
| **Engine** | `task_engine.hpp` | 3 | 50 ms | 512 words |
| **Sensors** | `task_sensors.hpp` | 3 | 100 ms | 512 words |
| **UART RX** | `main.cpp` | 2 | Event | 256 words |
| **CAN TX** | `task_can_tx.hpp` | 2 | 100 ms | 512 words |
| **Diag** | `task_diag.hpp` | 1 (lowest) | Event | 512 words |

### Queue topology

```
UART1 RX bytes
      │
      ▼
 g_control_queue (ControlEvent)
      │
      ▼
  T2 Sensors ──── reads throttle, fault inject, clear commands
      │
      │ publishes SensorReading every 100ms
      ▼
 g_sensor_queue (SensorReading)
      │
      ▼
  T3 CAN TX ──── builds CANFrames, transmits via UART0
      │
      │  (any task can post)
      ▼
 g_fault_queue (FaultEvent)
      │
      ▼
  T4 Diag ──── logs DTCs, sends 0x7E8 frames, writes UART1 debug log

 g_state (ECUState) ←── protected by g_state_mutex
      ↑                  T1 writes rpm/engine_state
      ├─────────────────  T2 writes sensor values + fault mask
      └─────────────────  T3/T4 read for frame data

 g_watchdog_counters[] ←── each task increments its slot every cycle
      ↑                     T5 checks all slots every 500ms
```

### Watchdog pattern

Each supervised task calls `watchdog_checkin(WatchID::ENGINE)` every cycle (no special API — just increments a `volatile uint8_t`). The watchdog compares counters to their previous values every 500 ms. Three consecutive missed windows = task declared hung → `FaultCode::WATCHDOG_RESET` posted → UART1 debug message printed.

---

## Fault Injection Walkthrough

1. Click **🌡 Inject Overheat** in the GUI
2. GUI sends byte `0x10` over TCP to UART1
3. `uart_ctrl_task` reads it, pushes `ControlEvent{INJECT_OVERHEAT}` to `g_control_queue`
4. T2 Sensors reads the event, sets `fault_overheat_ = true`
5. Next sensor tick: `coolant_f_` ramps +2°C per 100ms toward 130°C
6. When `coolant_f_ > 120.0`: `active_faults |= OVERHEAT`, `post_fault(OVERHEAT)` called
7. T3 CAN TX detects fault mask changed → sends `0x7E0` frame
8. T4 Diag receives `FaultEvent` from queue → adds P0217 to DTC log → sends `0x7E8` frame
9. T1 Engine reads `active_faults != 0` → transitions to FAULT state → RPM ramps to 0
10. GUI receives `0x7E0` frame → CANParser emits `faultMaskUpdated(0x01)` + `engineStateUpdated(3)`
11. Dashboard shows red fault badge + **⚠ FAULT** engine state + RPM dropping

---

## Extending with New Peripherals

Follow the same pattern used throughout:

1. **Find the QEMU sifive_u peripheral address** — check `hw/riscv/sifive_u.c` in the QEMU source or the SiFive U54 manual
2. **Write a HAL class** in `firmware/hal/` — model it on `hal_uart.hpp`: struct of `volatile uint32_t` registers, methods to read/write
3. **Create a new task** in `firmware/tasks/` — inherit the class-wrapping pattern, call `watchdog_checkin()` every cycle
4. **Add CAN IDs** to `ecu_protocol.hpp` (shared file — both firmware and GUI pick it up automatically)
5. **Add a decode case** in `qt_gui/src/CANParser.cpp` and a new typed signal
6. **Wire the signal** in `MainWindow::wireSignals()` to a dashboard slot

---

## Debugging

### Firmware (QEMU terminal)

```
[ECU] Booting...
[ECU] Queues ready
[ECU] Scheduler starting
[DIAG] DTCs active=0 total=0       ← heartbeat every 500ms
[DIAG] DTC P0217 (coolant) count=1 ← fault injected
[WDG] TASK HUNG: Engine (id=0)     ← watchdog fired
```

### Qt GUI

| Symptom | Cause | Fix |
|---|---|---|
| Status bar: "Not connected — command dropped" | TCP not connected | Check host IP and port 5001 |
| CAN Monitor empty | Serial port wrong / socat not running | Verify port name and `launch_qemu.sh` output |
| Fault badge appears at startup | Firmware running and sending fault frames | Expected if a fault was previously injected |
| RPM stuck at 0 | Engine in FAULT state | Click "Clear All Faults" |
| "CAN serial open failed" | Wrong COM port name | Use `\\.\pipe\ecu_can` or check Device Manager |

### QEMU didn't start

```bash
cat /tmp/qemu_ecu.log
file firmware/build/ecu_firmware
riscv64-unknown-elf-objdump -f firmware/build/ecu_firmware
```

### Check WSL IP (for GUI connection dialog)

```bash
ip route show | grep -oP 'src \K[\d.]+' | head -1
# or
hostname -I | awk '{print $1}'
```
