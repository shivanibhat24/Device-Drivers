# GPU-Style Warp Execution Model on FreeRTOS
### NUCLEO-F411RE (STM32F411RE @ 100 MHz)

---

## Overview

This project implements NVIDIA's GPU thread/warp/block execution model on a
bare-metal STM32 microcontroller running FreeRTOS.  Every core GPU concept is
mapped to a concrete RTOS primitive:

| GPU Concept         | This Implementation                                  |
|---------------------|------------------------------------------------------|
| Thread              | FreeRTOS task                                        |
| Warp (N threads)    | `WarpGroup<N>` — N tasks at equal priority           |
| Thread Block        | `ThreadBlock` — M warps sharing one barrier          |
| `__syncthreads()`   | `WarpBarrier::synchronise()` — two-phase EventGroup  |
| `__shared__` memory | `SharedMemoryBlock<T,N>` — SRAM + DMB fences         |
| Kernel launch       | `ThreadBlock::executeKernel(fn)`                     |
| `cudaDeviceSync()`  | Counting semaphore — host blocks until all done      |
| Lane ID             | `ctx->laneId` (0..WARP_SIZE-1)                       |
| Warp ID             | `ctx->warpId` (0..WARPS_PER_BLOCK-1)                 |

---

## Warp Lockstep Protocol

All WARP_SIZE threads within a warp run at the same FreeRTOS priority.
The scheduler round-robins them within a single time slice, which approximates
SIMT lockstep execution without requiring a custom scheduler.

The two-phase (ping/pong) barrier eliminates the race condition present in
naive single-bit barriers:

```
Thread arrives -> atomic increment of arrivalCount
  Last thread  -> flip phase, reset count, SET release bit
All threads    -> xEventGroupWaitBits on release bit (non-clearing)
  Last to leave -> atomic increment of departCount == totalThreads
                -> CLEAR release bit (safe for next round)
```

Using two alternating event bits (ping / pong) means a thread returning from
`synchronise()` and immediately calling it again will wait on the opposite bit,
so it cannot accidentally re-use the previous round's release signal.

---

## Demonstrated Kernels

### Kernel 1 — Parallel Reduction (Sum)

Input array is loaded into shared memory.  In log2(BLOCK_SIZE) steps, each
active thread adds a stride-distant neighbour.  A barrier separates every
stride step to ensure coherent reads.  Thread 0 holds the final sum.

```
Input:  1  2  3  4  5  6  7  8
        |--+  |--+  |--+  |--+   stride 4
           6     22            
           |-----+             stride 2 (conceptual)
                28            
Sum = 36
```

### Kernel 2 — 1-D Stencil (3-point average)

```
output[i] = ( input[i-1] + input[i] + input[i+1] ) / 3
```

Boundary threads clamp to the edge value (equivalent to halo guard cells).
One shared-memory load phase and one barrier precede the stencil computation.

### Kernel 3 — Inclusive Prefix Sum (Blelloch Scan)

Work-efficient parallel scan in O(N) work and O(log N) depth.

- Upsweep: build partial-sum tree (reduce phase)
- Zero the last element (converts to exclusive scan)
- Downsweep: distribute partial sums
- Add original input to convert from exclusive to inclusive

```
Input:  1  1  1  1  1  1  1  1
Output: 1  2  3  4  5  6  7  8
```

---

## File Structure

```
your_project/
  Core/
    Src/
      main.c            <- add  app_main();  before vTaskStartScheduler()
      app.cpp           <- this file
    Inc/
      app.h             <- declares app_main()
```

`app.cpp` and `app.h` drop directly into `Core/Src` and `Core/Inc`.
No CMakeLists or Makefile changes are needed beyond adding `app.cpp` to
the build (STM32CubeIDE picks up all `.cpp` files in `Core/Src` automatically).

---

## STM32CubeIDE / .ioc Configuration

Open your `.ioc` file and apply every setting listed below, then regenerate
code before building.

### 1. FreeRTOS

| Path | Setting | Value |
|------|---------|-------|
| Middleware > FreeRTOS | Interface | CMSIS_V1 |
| FreeRTOS > Config > USE_PREEMPTION | Enabled | 1 |
| FreeRTOS > Config > USE_TIME_SLICING | Enabled | 1 |
| FreeRTOS > Config > configTICK_RATE_HZ | Value | 1000 |
| FreeRTOS > Config > configMAX_PRIORITIES | Value | 7 |
| FreeRTOS > Config > configTOTAL_HEAP_SIZE | Value | 20480 |
| FreeRTOS > Config > configUSE_COUNTING_SEMAPHORES | Enabled | 1 |
| FreeRTOS > Config > configUSE_EVENT_GROUPS | Enabled | 1 |
| FreeRTOS > Config > configSUPPORT_DYNAMIC_ALLOCATION | Enabled | 1 |

> **Heap size note.**  With WARP_SIZE=4 and WARPS_PER_BLOCK=2, the runtime
> allocates 8 task stacks (256 words each), 3 queue/semaphore objects, and
> 1 event group.  20 KB is comfortable.  If you increase WARP_SIZE or
> THREAD_STACK_WORDS, raise configTOTAL_HEAP_SIZE proportionally.

### 2. USART2 (Virtual COM over ST-Link USB)

| Path | Setting | Value |
|------|---------|-------|
| Connectivity > USART2 | Mode | Asynchronous |
| USART2 > Parameter Settings > Baud Rate | 115200 |
| USART2 > Parameter Settings > Word Length | 8 Bits |
| USART2 > Parameter Settings > Parity | None |
| USART2 > Parameter Settings > Stop Bits | 1 |
| USART2 > GPIO Settings > PA2 | USART2_TX |
| USART2 > GPIO Settings > PA3 | USART2_RX |

### 3. GPIO — LD2 (User LED)

PA5 is already configured as `LD2` on the Nucleo board.  Verify:

| Path | Setting | Value |
|------|---------|-------|
| System Core > GPIO > PA5 | GPIO output level | Low |
| PA5 > User Label | LD2 |

### 4. Clock Configuration

| Setting | Value |
|---------|-------|
| PLL Source | HSI (16 MHz) |
| PLLM | 16 |
| PLLN | 200 |
| PLLP | /2 |
| System Clock | PLLCLK |
| HCLK | 100 MHz |
| APB1 Prescaler | /2  (50 MHz) |
| APB2 Prescaler | /1  (100 MHz) |

### 5. C++ Runtime (mandatory for `std::function`, atomics, placement new)

In STM32CubeIDE, right-click the project:

```
Properties > C/C++ Build > Settings
  > MCU/MPU G++ Compiler > Miscellaneous
      Other flags: -std=c++17
  > MCU/MPU G++ Linker > Miscellaneous
      Other flags: -lstdc++ -lm
```

Also confirm the linker script has at least 8 KB of heap
(`_Min_Heap_Size = 0x2000` in `STM32F411RETX_FLASH.ld`).

### 6. Calling app_main from main.c

After code regeneration, open `Core/Src/main.c` and add the call:

```c
/* USER CODE BEGIN Includes */
#include "app.h"
/* USER CODE END Includes */

/* inside main(), after all MX_*_Init() calls: */
/* USER CODE BEGIN 2 */
app_main();   /* does not return */
/* USER CODE END 2 */
```

Do **not** call `vTaskStartScheduler()` from `main.c` after this — `app_main`
handles that internally.

---

## Serial Output

Connect a terminal (115200 8N1) to the Nucleo's ST-Link USB port (appears as
a CDC COM port on Windows, `/dev/ttyACM0` on Linux).

Expected output:

```
=== GPU Warp Execution Model on FreeRTOS ===
Target: NUCLEO-F411RE  |  Warp size: 4  |  Block size: 8
Barrier: two-phase EventGroup (ping/pong)
----------------------------------------------
[Kernel 1] Parallel Reduction (sum)
  Input : 1, 2, 3, 4, 5, 6, 7, 8
  Sum = 36
[Kernel 2] 1-D Stencil (3-point average)
  Input : 10, 20, 30, 40, 50, 60, 70, 80
  Output: 13, 20, 30, 40, 50, 60, 70, 76
[Kernel 3] Inclusive Prefix Sum (Blelloch scan)
  Input : 1, 1, 1, 1, 1, 1, 1, 1
  Prefix: 1, 2, 3, 4, 5, 6, 7, 8
----------------------------------------------
All kernels completed. Idle.
```

LD2 (green LED) blinks at 1 Hz after all kernels complete.

---

## Tuning Parameters

All compile-time knobs live in the `cfg` namespace at the top of `app.cpp`:

| Constant | Default | Effect |
|----------|---------|--------|
| `WARP_SIZE` | 4 | Threads per warp (FreeRTOS tasks per WarpGroup) |
| `WARPS_PER_BLOCK` | 2 | Warps assembled into one ThreadBlock |
| `BLOCK_SIZE` | 8 | Derived: WARP_SIZE * WARPS_PER_BLOCK |
| `SHARED_MEM_WORDS` | 32 | int32_t elements in the shared memory region |
| `THREAD_STACK_WORDS` | 256 | Stack depth per warp thread (words) |
| `THREAD_PRIORITY` | 2 | FreeRTOS priority for all warp threads |

Increasing `WARP_SIZE` to 8 and `WARPS_PER_BLOCK` to 4 (32-thread block,
matching a real NVIDIA warp) is feasible if `configTOTAL_HEAP_SIZE` is raised
to at least 40 KB.  The F411RE has 128 KB SRAM so this is well within budget.

---

## Design Notes

**Why equal-priority tasks approximate lockstep:**
FreeRTOS with `USE_TIME_SLICING=1` will preempt a running task of a given
priority at every tick interrupt and switch to the next ready task of equal
priority.  All warp threads share the same priority, so they interleave within
one tick cycle.  This is not true SIMT — threads can diverge between barriers —
but it is the closest approximation achievable without a custom scheduler.

**Why DMB fences are needed:**
Cortex-M4 is a weakly-ordered architecture: the processor can re-order stores
to separate memory locations.  The `SharedMemoryBlock` issues `__DMB()`
(Data Memory Barrier) after every store to prevent the compiler and CPU from
reordering writes to shared SRAM across the barrier boundary.

**Why a two-phase barrier and not a simple semaphore:**
A single binary semaphore would require the last thread to give N-1 times and
is hard to reset safely.  An EventGroup with a single "done" bit suffers from
the ABA problem: a fast thread can loop back and observe a stale set bit from
the previous barrier round before the last departing thread has cleared it.
The ping/pong alternation eliminates this without needing a mutex.

---

## License

MIT — free to use, modify, and redistribute with attribution.
