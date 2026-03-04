# Lua 5.4 Embedded Scripting Engine
### STM32 Nucleo F411RE · FreeRTOS · C++

A Lua 5.4 interpreter running on a Cortex-M4 microcontroller, allowing hardware to be scripted live over a serial terminal — no recompilation, no reflashing. Send Lua code over UART and watch it execute on real hardware in real time.

---

## What This Is

Most embedded firmware is static: change behavior, recompile, reflash, repeat. This project breaks that cycle by embedding a full Lua 5.4 interpreter into a FreeRTOS application. GPIO pins, the ADC, the LED, and delays are all exposed as Lua globals. You type a script into a serial terminal and the MCU runs it immediately.

This is useful for hardware bringup, automated test scripts, and field-updatable device behavior. It is also a demonstration of several non-trivial embedded engineering concepts: custom memory allocators, mixed C/C++ firmware architecture, interrupt-driven UART with FreeRTOS queues, and safe embedding of a scripting engine on a resource-constrained MCU.

---

## Hardware

| Item | Detail |
|---|---|
| Board | STM32 Nucleo F411RE |
| MCU | STM32F411RE — Cortex-M4 @ 84 MHz |
| Flash | 512 KB |
| RAM | 128 KB |
| UART | USART2 via USB virtual COM port (ST-Link) |
| LED | LD2 on PA5 |
| Button | B1 on PC13 |

No external components are required.

---

## Project Structure

```
Core/
├── Src/
│   ├── main.c          ← CubeMX generated, untouched
│   ├── app.cpp         ← This project (LuaEngine, tasks, allocator)
│   └── lua/            ← Lua 5.4 source files (see setup)
│       ├── lua.h
│       ├── lualib.h
│       ├── lauxlib.h
│       └── *.c         ← All Lua sources except lua.c and luac.c
└── Inc/
    └── main.h
```

### Architecture

```
main.c  (CubeMX-owned)
  └── cpp_main()
        ├── uart_task  [priority 3] — receives bytes via ISR → byte_queue
        │                             assembles scripts → script_queue
        └── lua_task   [priority 2] — pops from script_queue
                                      dispatches to LuaEngine
                                      prints result over UART
```

Two FreeRTOS tasks, two queues, one mutex. The UART task has higher priority than the Lua task so bytes are never dropped while a script is executing.

---

## Memory Layout

RAM is the constraint on this MCU. Every allocation is accounted for.

| Region | Size | Notes |
|---|---|---|
| Lua static pool | 32 KB | Custom allocator, never touches system heap |
| `lua_task` stack | 6 KB | Lua VM is stack-heavy |
| `uart_task` stack | 512 B | Lightweight byte assembler |
| FreeRTOS heap | ~60 KB | Remaining after globals and stacks |

**The key design decision:** Lua's allocator is replaced with a custom first-fit allocator backed by a static 32 KB array (`lua_pool`). Lua never calls `malloc()` or `free()` — it only allocates from this pool. This eliminates heap fragmentation risk from Lua's allocation patterns and keeps the FreeRTOS heap clean for everything else.

In `FreeRTOSConfig.h`, set:
```c
#define configTOTAL_HEAP_SIZE  (48 * 1024)
```

---

## Setup

### 1. Get Lua 5.4 sources

Download from [lua.org/download](https://www.lua.org/download.html). Extract the archive (use 7-Zip on Windows). Copy all `.c` and `.h` files from the `src/` folder into `Core/Src/lua/` in your project.

### 2. Exclude standalone entry points from build

Right-click each of these in CubeIDE → Resource Configurations → Exclude from Build:
- `lua.c` — standalone interpreter `main()`
- `luac.c` — standalone compiler `main()`
- `ltests.c` — test harness (if present)

These define their own `main()` and will cause linker errors if compiled.

### 3. Add include path

Project Properties → C/C++ Build → Settings:
- Under **MCU GCC Compiler** → Include Paths → add `../Core/Src/lua`
- Under **MCU G++ Compiler** → Include Paths → add `../Core/Src/lua`

Both compilers need the path since `main.c` and `app.cpp` both include Lua headers.

### 4. Add preprocessor define

In the same Settings panel, under **MCU GCC Compiler** → Preprocessor Defines, add:
```
LUA_32BITS
```
This makes `lua_Integer` a 32-bit type instead of 64-bit, which saves RAM on a 32-bit MCU and avoids soft-emulation of 64-bit arithmetic.

### 5. Hook into main.c

In `main.c`, inside the CubeMX user code sections:

```c
/* USER CODE BEGIN PFP */
extern void cpp_main(void);
/* USER CODE END PFP */
```

```c
/* USER CODE BEGIN 2 — after all MX_xxx_Init() calls, before the while loop */
cpp_main();
/* USER CODE END 2 */
```

### 6. Add the gettimeofday stub

Lua 5.4 calls `gettimeofday` internally. On bare metal this is not implemented. Add this to `app.cpp` to silence the linker warning and provide a working implementation:

```cpp
#include <sys/time.h>

extern "C" int _gettimeofday(struct timeval* tv, void* tz) {
    if (tv) {
        tv->tv_sec  = xTaskGetTickCount() / configTICK_RATE_HZ;
        tv->tv_usec = (xTaskGetTickCount() % configTICK_RATE_HZ) * 1000;
    }
    return 0;
}
```

### 7. Build and flash

Build with **Ctrl+B**. Flash via the debug configuration (Run → Debug Configurations → STM32 Cortex-M C/C++ Application).

---

## Serial Terminal Setup

Connect to the Nucleo's virtual COM port at **115200 baud, 8N1**.

On Windows: PuTTY or TeraTerm. On Linux/macOS: `screen /dev/ttyACM0 115200`.

On reset you will see:
```
[UART] Ready. Send Lua one-liners or ---BEGIN---/---END--- blocks.
[UART] Preloaded scripts: blink, adc_poll, uptime_check, gpio_toggle
[UART] Run with: run:blink

[LUA] Engine ready. Send Lua script over UART.
```

---

## Usage

### Run a preloaded Flash script

Scripts stored in Flash can be run by name without sending any Lua code:

```
run:blink
run:adc_poll
run:uptime_check
run:gpio_toggle
```

### Send an inline one-liner

Type a single line of Lua and press Enter:

```lua
uart.print("Hello from Lua! Uptime: " .. uptime() .. "ms\n")
```

```lua
led.on()
```

```lua
uart.print(string.format("sin(pi/4) = %.4f\n", math.sin(math.pi/4)))
```

### Send a multi-line script

Wrap your script between the begin and end markers:

```
---BEGIN---
for i = 1, 5 do
  led.on()
  delay(300)
  led.off()
  delay(300)
  uart.print("Blink " .. i .. " of 5\n")
end
uart.print("Done.\n")
---END---
```

---

## Lua API Reference

All hardware is exposed as Lua globals. Scripts have access to the following:

### `gpio`

```lua
gpio.set(pin, value)   -- set pin HIGH (1) or LOW (0)
gpio.get(pin)          -- read pin state, returns 0 or 1
```

Pin numbers map to physical Nucleo pins:

| Lua pin | MCU pin | Nucleo label |
|---|---|---|
| 0 | PA0 | A0 |
| 1 | PA1 | A1 |
| 2 | PA4 | A2 |
| 3 | PB0 | A3 |
| 4 | PC1 | A4 |
| 5 | PC0 | A5 |
| 6 | PA5 | LD2 (LED) |
| 7 | PA9 | D8 |

### `led`

```lua
led.on()    -- turn LD2 on
led.off()   -- turn LD2 off
```

### `adc`

```lua
local raw = adc.read(channel)   -- channel 0–15, returns 12-bit raw value (0–4095)
local mv  = raw * 3300 / 4095   -- convert to millivolts (3.3V reference)
```

### `uart`

```lua
uart.print(str)   -- send string over UART
```

### `delay`

```lua
delay(ms)   -- suspend current task for ms milliseconds (calls vTaskDelay)
```

### `uptime`

```lua
local ms = uptime()   -- milliseconds since boot
```

### Standard Lua libraries available

`string`, `table`, `math`, `_G` (base). The `io`, `os`, and `package` libraries are intentionally not loaded — there is no filesystem.

---

## Example Scripts

**Read ADC and convert to voltage:**
```lua
local raw = adc.read(0)
local mv  = math.floor(raw * 3300 / 4095)
uart.print(string.format("PA0: %d raw, %d mV\n", raw, mv))
```

**Blink LED with decreasing interval:**
```lua
local delay_ms = 500
for i = 1, 8 do
  led.on()
  delay(delay_ms)
  led.off()
  delay(delay_ms)
  delay_ms = math.floor(delay_ms * 0.7)
end
```

**Log uptime every second for 5 seconds:**
```lua
for i = 1, 5 do
  uart.print("Uptime: " .. uptime() .. " ms\n")
  delay(1000)
end
```

**Toggle a GPIO pin rapidly and count iterations:**
```lua
local count = 0
local start = uptime()
while uptime() - start < 2000 do
  gpio.set(0, 1)
  gpio.set(0, 0)
  count = count + 1
end
uart.print("Toggled " .. count .. " times in 2 seconds\n")
```

---

## Extending the API

Adding a new Lua function takes two steps.

**Step 1 — Write a `lua_CFunction`:**

```cpp
static int l_pwm_set(lua_State* L) {
    lua_Integer duty = luaL_checkinteger(L, 1);  // first argument
    if (duty < 0 || duty > 1000)
        return luaL_error(L, "pwm.set: duty must be 0–1000");
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, static_cast<uint32_t>(duty));
    return 0;  // number of return values pushed onto Lua stack
}
```

**Step 2 — Register it in `LuaEngine::init()`:**

```cpp
lua_newtable(L_);
lua_pushcfunction(L_, l_pwm_set); lua_setfield(L_, -2, "set");
lua_setglobal(L_, "pwm");
```

Now from Lua:
```lua
pwm.set(512)
```

The same pattern works for any peripheral: timers, SPI, I2C, DAC. Each peripheral gets its own table of functions registered as a global.

---

## How the Allocator Works

Lua requires a user-supplied allocator function. By default it uses the system `malloc`. On a microcontroller this is dangerous: Lua's allocation patterns (many small short-lived allocations during script execution) will fragment the heap rapidly.

This project uses a custom first-fit allocator backed by a 32 KB static array:

```
lua_pool[32768]
─────────────────────────────────────────────
[ Block header | user data ] → [ Block header | user data ] → ...
```

Each `Block` header stores the size of the following user data and a pointer to the next free block. On `alloc`, the allocator walks the free list and finds the first block large enough. On `free`, it re-inserts the block in address order and coalesces adjacent free blocks to prevent fragmentation over time.

This keeps Lua completely isolated from the FreeRTOS heap. Even if a script leaks memory or exhausts the pool, the rest of the system is unaffected.

---

## FreeRTOS Configuration Requirements

In `FreeRTOSConfig.h`, ensure the following are set:

```c
#define configTOTAL_HEAP_SIZE           (48 * 1024)
#define configMINIMAL_STACK_SIZE        256
#define configUSE_MUTEXES               1
#define configUSE_COUNTING_SEMAPHORES   1
#define configUSE_TASK_NOTIFICATIONS    1
#define configCHECK_FOR_STACK_OVERFLOW  2
#define configUSE_MALLOC_FAILED_HOOK    1
```

---

## Engineering Notes

**Why uart_task has higher priority than lua_task:** A Lua script can run for hundreds of milliseconds (loops, delays). During that time, if uart_task were lower priority, incoming UART bytes would be dropped. By giving uart_task the higher priority, it preempts lua_task on every received byte, drains the byte into the queue, then yields back. No bytes are lost regardless of how long a script runs.

**Why scripts are heap-allocated and passed as pointers through the queue:** The alternative — copying 512 bytes directly into the queue — would require a queue item size of 512 bytes and waste memory on every queue slot. Instead, the uart_task allocates exactly as much as needed with `pvPortMalloc`, posts the pointer (8 bytes) into the queue, and the lua_task frees it after execution.

**Why `io`, `os`, and `package` libs are excluded:** These libraries make assumptions about the existence of a filesystem, environment variables, and dynamic library loading. Opening them on bare metal either crashes immediately or silently fails in confusing ways. Excluding them keeps the Lua environment honest about what the hardware can actually do.

---

## Limitations

- Maximum script size: 512 bytes per transmission
- Lua heap pool: 32 KB — sufficient for loops, tables, and string operations but not large data structures
- No persistent variables between script executions (the Lua state is shared across runs, so globals do persist within a session but reset on power cycle)
- No coroutines (they require significant additional stack space)
- `print()` is not remapped — use `uart.print()` instead

---

Project code (`app.cpp`) is provided as-is for educational and portfolio use.

Made with ✨✨ by Shivani!
