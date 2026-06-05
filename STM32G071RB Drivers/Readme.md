# STM32G071RB — Bare-Metal Driver Development Guide
> **Board:** NUCLEO-G071RB &nbsp;|&nbsp; **MCU:** STM32G071RBT6 &nbsp;|&nbsp; **Core:** ARM Cortex-M0+ @ 64 MHz
> **Toolchain:** PlatformIO + VSCode + STM32Cube framework

---

## Essential Documents (Always Have Open)

| Document | What It Covers | Link |
|---|---|---|
| **RM0444** — Reference Manual | Every register, every bit, every peripheral. This is your bible. | [ST.com](https://www.st.com/resource/en/reference_manual/dm00371828-stm32g0x1-advanced-arm-based-32-bit-mcus-stmicroelectronics.pdf) |
| **DS12232** — MCU Datasheet | Pin functions, electrical specs, peripheral feature overview | [ST.com](https://www.st.com/resource/en/datasheet/STM32G071RB.pdf) |
| **UM2324** — Board User Manual | Nucleo-64 pinout, ST-LINK, jumpers, schematic | [ST.com](https://www.st.com/resource/en/user_manual/um2324-stm32-nucleo64-boards-mb1360-stmicroelectronics.pdf) |
| **MB1360 Schematic** | Full board schematic | [ST.com](https://www.st.com/resource/en/schematic_pack/mb1360-g071rb-c01_schematic.pdf) |

---

## MCU Quick Reference

| Property | Value |
|---|---|
| Core | ARM Cortex-M0+, 32-bit |
| Max Clock | 64 MHz |
| Flash | 128 KB |
| SRAM | 36 KB |
| Supply | 1.7 – 3.6 V |
| DMA | 7-channel DMA1 + DMAMUX |
| Timers | 14 (TIM1/3/14/15/16/17 + TIM2/6/7 + LPTIM1/2) |
| ADC | 12-bit, up to 16 ext. channels |
| USART/UART | 4x USART + 2x LPUART |
| SPI | 2x (up to 32 Mbit/s) |
| I2C | 2x |
| DAC | 2-channel, 12-bit |

---

## The Mental Model for Every Driver

Before writing a single line of code for any peripheral, answer these five questions **in order**. Each maps to a specific chapter in RM0444.

```
1. WHERE are the registers?      →  RM0444 Chapter 2  (Memory Map)
2. What CLOCK do I enable?       →  RM0444 Chapter 5  (RCC)
3. Which GPIO mode do I need?    →  RM0444 Chapter 9  (GPIO) + DS12232 Table 13
4. How do I CONFIGURE it?        →  RM0444 peripheral chapter (e.g. Ch.10 DMA, Ch.28 USART)
5. Which INTERRUPTS fire?        →  RM0444 Chapter 12 (NVIC) + peripheral ISR table
```

Every register field in your driver must map 1-to-1 to a row in the RM. If it's not in the RM, don't guess it.

---

## Step 1 — Find the Register Base Address (RM0444 Ch. 2)

Open RM0444 to **Table 1: STM32G0x1 memory map**. Every peripheral has a base address. These are what CMSIS `stm32g0xx.h` defines under the hood.

Key base addresses for this MCU:

| Peripheral | Base Address | CMSIS Symbol |
|---|---|---|
| GPIOA | `0x50000000` | `GPIOA` |
| GPIOB | `0x50000400` | `GPIOB` |
| GPIOC | `0x50000800` | `GPIOC` |
| RCC | `0x40021000` | `RCC` |
| DMA1 | `0x40020000` | `DMA1` |
| DMAMUX1 | `0x40020800` | `DMAMUX1` |
| TIM1 | `0x40012C00` | `TIM1` |
| TIM2 | `0x40000000` | `TIM2` |
| TIM6 | `0x40001000` | `TIM6` |
| USART1 | `0x40013800` | `USART1` |
| USART2 | `0x40004400` | `USART2` |
| SPI1 | `0x40013000` | `SPI1` |
| I2C1 | `0x40005400` | `I2C1` |
| ADC1 | `0x40012400` | `ADC1` |
| DAC1 | `0x40007400` | `DAC1` |

You never hard-code these yourself — always use the CMSIS symbol. But knowing what the symbol *resolves to* lets you verify you're writing to the right place.

---

## Step 2 — Enable the Clock (RM0444 Ch. 5 — RCC)

**This is the #1 silent bug in embedded programming.** Writing to a peripheral's register when its clock is disabled does nothing. No error, no fault — the write is simply ignored.

Every peripheral is gated behind one of these RCC enable registers:

| Bus | Register | Peripherals |
|---|---|---|
| AHB | `RCC->AHBENR` | DMA1, Flash, CRC, GPIO |
| APB1 | `RCC->APBENR1` | TIM2/3/6/7, USART2/3/4, SPI2, I2C1/2, DAC1, PWR |
| APB2 | `RCC->APBENR2` | SYSCFG, TIM1/14/15/16/17, USART1, SPI1, ADC |
| IOP | `RCC->IOPENR` | GPIOA through GPIOF |

**Pattern: always the first line of any driver init function.**

```c
// Examples:
RCC->IOPENR  |= RCC_IOPENR_GPIOAEN;     // GPIOA
RCC->AHBENR  |= RCC_AHBENR_DMA1EN;      // DMA1 (also enables DMAMUX)
RCC->APBENR2 |= RCC_APBENR2_USART1EN;   // USART1
RCC->APBENR2 |= RCC_APBENR2_TIM1EN;     // TIM1
RCC->APBENR2 |= RCC_APBENR2_ADCEN;      // ADC
RCC->APBENR1 |= RCC_APBENR1_TIM6EN;     // TIM6
RCC->APBENR1 |= RCC_APBENR1_DAC1EN;     // DAC1
```

> **How to find the bit:** In RM0444 Ch.5, search for your peripheral name in the `AHBENR`, `APBENR1`, or `APBENR2` register bit table. The CMSIS macro follows the pattern `RCC_<REGISTER>_<PERIPHERALNAME>EN`.

---

## Step 3 — Configure GPIO (RM0444 Ch. 9)

Most peripherals use GPIO pins. Before enabling the peripheral, the pins need the right mode.

### GPIO Registers

Each GPIO port has these registers (example: `GPIOA`):

| Register | Purpose |
|---|---|
| `GPIOx->MODER` | Pin mode: Input / Output / Alternate / Analog |
| `GPIOx->OTYPER` | Output type: Push-pull or Open-drain |
| `GPIOx->OSPEEDR` | Output speed: Low / Medium / High / Very-High |
| `GPIOx->PUPDR` | Pull-up / Pull-down / None |
| `GPIOx->IDR` | Input data (read-only) |
| `GPIOx->ODR` | Output data |
| `GPIOx->BSRR` | Atomic bit set/reset |
| `GPIOx->AFR[0/1]` | Alternate function select (pins 0-7 / 8-15) |

### MODER Values (2 bits per pin)

```
00 = Input
01 = General-purpose output
10 = Alternate function
11 = Analog (reset state, also used for ADC)
```

### How to Find the Alternate Function Number

1. Open **DS12232 Table 13** — "Alternate function mapping"
2. Find your pin (e.g. PA9) and the peripheral (e.g. USART1_TX)
3. Note the AF column number (e.g. AF1)

```c
// Example: configure PA9 as USART1_TX (AF1), PA10 as USART1_RX (AF1)
RCC->IOPENR |= RCC_IOPENR_GPIOAEN;

// Set mode to Alternate Function (10) for pins 9 and 10
GPIOA->MODER &= ~((0x3 << (9*2)) | (0x3 << (10*2)));
GPIOA->MODER |=  ((0x2 << (9*2)) | (0x2 << (10*2)));

// Set AFR: pins 8-15 are in AFR[1], offset = pin - 8
GPIOA->AFR[1] &= ~((0xF << ((9-8)*4)) | (0xF << ((10-8)*4)));
GPIOA->AFR[1] |=  ((0x1 << ((9-8)*4)) | (0x1 << ((10-8)*4)));  // AF1
```

> **AFR index rule:** pins 0–7 → `AFR[0]`, pins 8–15 → `AFR[1]`. Within each, the field is `(pin % 8) * 4` bits wide, 4 bits each.

---

## Step 4 — Configure the Peripheral (RM0444 Peripheral Chapter)

Open the chapter for your specific peripheral. Every chapter follows the same structure:

1. **Introduction** — what the peripheral does
2. **Main features** — capability list
3. **Functional description** — how it works, state machines, timing diagrams
4. **Registers** — bit-by-bit description of every register

**The register section is the most important part.** Read every bit field for any register you touch.

### Key register reading habits

- Note which bits are **read-only** (RO) — writing them does nothing or causes a fault
- Note which bits are **write-1-to-clear** (rc_w1) — like status flags
- Note which bits have **hardware-set** behavior — the peripheral writes these itself
- Note the **reset value** — the register's state after a reset, so you know what to change vs. what's already correct
- Always **disable the peripheral** (`EN=0`) before reconfiguring — most peripherals ignore or fault on mid-run config changes

---

## Step 5 — Configure Interrupts (RM0444 Ch. 12 — NVIC)

Every peripheral chapter has an "Interrupts" section listing which flags trigger which IRQ lines.

```c
// 1. Enable the interrupt in the peripheral's own register (e.g. CR1, CCR)
USART1->CR1 |= USART_CR1_RXNEIE;   // Enable RXNE interrupt in USART

// 2. Set priority (0 = highest, 3 = lowest on Cortex-M0+)
NVIC_SetPriority(USART1_IRQn, 1);

// 3. Enable the IRQ line in NVIC
NVIC_EnableIRQ(USART1_IRQn);

// 4. Write the handler with the exact weak-symbol name from startup file
void USART1_IRQHandler(void) {
    if (USART1->ISR & USART_ISR_RXNE) {
        uint8_t data = USART1->RDR;   // Reading RDR auto-clears RXNE
    }
}
```

> **IRQ names:** Find them in `stm32g071xx.h` — look for the `IRQn_Type` enum. The handler name is always `<IRQn_name>Handler` with `IRQn` stripped.

> **Cortex-M0+ has 2-bit priority** — only 4 levels (0–3). No sub-priorities.

---

## DMA Driver Deep Dive

### Architecture

The STM32G071 uses a **Type 3 DMA** architecture with two layers:

```
Peripheral request  →  DMAMUX  →  DMA1 Channel  →  Memory/Peripheral
```

**DMAMUX** (Chapter 11) is a flexible multiplexer that lets you route *any* peripheral request to *any* DMA channel. On older STM32s (F1/F4), each channel was hardwired to specific peripherals. On the G0, you explicitly configure this mapping.

### The Off-By-One Rule

> ⚠️ **Critical:** DMAMUX channels are **0-indexed**. DMA channels are **1-indexed**.
> DMAMUX channel `N` always maps to DMA1 channel `N+1`.

```c
// DMA channel 1 → DMAMUX channel index 0
DMAMUX1_Channel0[channel - 1].CCR = request_id;
```

### DMA Channel Register Layout (RM0444 Table 75)

Each DMA channel occupies a 0x14-byte block starting at `DMA1_BASE + 0x08`:

```
Offset 0x08 + (ch-1)*0x14:
  +0x00  CCR    — Channel Configuration Register
  +0x04  CNDTR  — Number of Data to Transfer
  +0x08  CPAR   — Peripheral Address
  +0x0C  CMAR   — Memory Address
  +0x10  (reserved)
```

### CCR Bit Reference

| Bit(s) | Name | Description |
|---|---|---|
| 0 | `EN` | Channel enable — **set last, always** |
| 1 | `TCIE` | Transfer complete interrupt enable |
| 2 | `HTIE` | Half-transfer interrupt enable |
| 3 | `TEIE` | Transfer error interrupt enable |
| 4 | `DIR` | Direction: 0=periph→mem, 1=mem→periph |
| 5 | `CIRC` | Circular mode (auto-reloads CNDTR) |
| 6 | `PINC` | Peripheral address increment |
| 7 | `MINC` | Memory address increment |
| 9:8 | `PSIZE` | Peripheral data size: 00=8b, 01=16b, 10=32b |
| 11:10 | `MSIZE` | Memory data size: 00=8b, 01=16b, 10=32b |
| 13:12 | `PL` | Priority: 00=low, 01=medium, 10=high, 11=very-high |
| 14 | `MEM2MEM` | Memory-to-memory mode (no peripheral trigger) |

### ISR / IFCR Flag Layout

`DMA1->ISR` and `DMA1->IFCR` are single 32-bit registers covering all 7 channels. Each channel gets 4 bits at offset `(channel-1) * 4`:

```
Bits 3:0   = Channel 1  (GIF1, TCIF1, HTIF1, TEIF1)
Bits 7:4   = Channel 2
...
Bits 27:24 = Channel 7
```

- **GIF** — Global interrupt flag (set if any flag is set)
- **TCIF** — Transfer complete
- **HTIF** — Half transfer
- **TEIF** — Transfer error

Flags do **not** auto-clear. You must manually write 1 to the corresponding bit in `DMA1->IFCR`.

### DMAMUX Request IDs (RM0444 Table 78)

| Peripheral | Request ID |
|---|---|
| No request (mem-to-mem / software) | 0 |
| ADC | 5 |
| DAC CH1 | 6 |
| DAC CH2 | 7 |
| TIM6 UPDATE | 10 |
| TIM7 UPDATE | 11 |
| SPI1 RX | 16 |
| SPI1 TX | 17 |
| SPI2 RX | 18 |
| SPI2 TX | 19 |
| I2C1 RX | 10 |
| I2C1 TX | 11 |
| USART1 RX | 50 |
| USART1 TX | 51 |
| USART2 RX | 52 |
| USART2 TX | 53 |
| ADC (alternate) | 5 |

> Full table is in **RM0444, Section 11.4.4, Table 78**.

### Complete DMA Initialization Sequence

```c
// Correct order — never skip or reorder these steps

// 1. Enable DMA1 clock (also gates DMAMUX)
RCC->AHBENR |= RCC_AHBENR_DMA1EN;

// 2. Disable the channel (must be off before configuring)
DMA1_Channel1->CCR &= ~DMA_CCR_EN;

// 3. Configure DMAMUX — maps peripheral request to this channel
//    ch=1 → DMAMUX index 0
DMAMUX1_Channel0[0].CCR = 51;  // USART1_TX request ID

// 4. Set transfer addresses
DMA1_Channel1->CPAR = (uint32_t)&USART1->TDR;   // Peripheral (fixed)
DMA1_Channel1->CMAR = (uint32_t)tx_buffer;        // Memory (incrementing)

// 5. Set transfer count
DMA1_Channel1->CNDTR = buffer_length;

// 6. Configure CCR (direction, sizes, mode, interrupts)
DMA1_Channel1->CCR = DMA_CCR_DIR      // mem → periph
                   | DMA_CCR_MINC     // increment memory address
                   | DMA_CCR_TCIE     // interrupt on complete
                   | DMA_CCR_TEIE;    // interrupt on error

// 7. Enable — always last
DMA1_Channel1->CCR |= DMA_CCR_EN;
```

---

## PlatformIO Project Setup

### platformio.ini

```ini
[env:nucleo_g071rb]
platform  = ststm32
board     = nucleo_g071rb
framework = stm32cube

; Optional: enable serial monitor on virtual COM port
monitor_speed = 115200
```

### Build & Flash Commands

```bash
pio run                     # Compile
pio run --target upload     # Flash via on-board ST-LINK
pio run --target clean      # Clean build artifacts
pio device monitor          # Open serial monitor
pio run --target upload && pio device monitor  # Flash + monitor
```

### Header Includes

```c
#include "stm32g0xx.h"   // CMSIS device header — all registers, bit masks, IRQn names
```

The `stm32cube` framework provides:
- `stm32g0xx.h` — peripheral structs and register bit definitions
- `system_stm32g0xx.h` — `SystemCoreClock` variable, `SystemInit()`
- `core_cm0plus.h` — NVIC, SysTick, core instructions

---

## Common Driver Patterns

### Pattern: Read-Modify-Write (safe bit set)

```c
// GOOD — preserves other bits
GPIOA->MODER |= (0x1 << (5*2));

// BAD — clears all other pins
GPIOA->MODER = (0x1 << (5*2));
```

### Pattern: Clear a multi-bit field then set it

```c
// Clear bits 10:9 (MSIZE field, 2 bits at position 10)
DMA1_Channel1->CCR &= ~(0x3 << 10);
// Set to 01 (16-bit)
DMA1_Channel1->CCR |=  (0x1 << 10);
```

### Pattern: Atomic GPIO toggle (no read-modify-write needed)

```c
// BSRR: bits 15:0 = set, bits 31:16 = reset — single-cycle atomic
GPIOA->BSRR = (1 << 5);          // Set PA5
GPIOA->BSRR = (1 << (5 + 16));   // Clear PA5
```

### Pattern: Polling a status flag

```c
// Wait for TXE (transmit buffer empty)
while (!(USART1->ISR & USART_ISR_TXE_TXFNF));
USART1->TDR = byte;
```

### Pattern: Clear a write-1-to-clear flag

```c
// USART1 ORE (overrun error) — write 1 to ORECF in ICR
USART1->ICR |= USART_ICR_ORECF;
```

### Pattern: Disable peripheral before reconfiguring

```c
// Never change config while running
USART1->CR1 &= ~USART_CR1_UE;    // Disable
// ... change baud rate, data bits, etc. ...
USART1->CR1 |= USART_CR1_UE;     // Re-enable
```

---

## Peripheral Init Checklists

Use these as quick sanity checks when starting a new driver.

### GPIO Output
- [ ] `RCC->IOPENR` — enable port clock
- [ ] `GPIOx->MODER` — set to `01` (output)
- [ ] `GPIOx->OTYPER` — push-pull (`0`) or open-drain (`1`)
- [ ] `GPIOx->OSPEEDR` — set appropriate speed
- [ ] `GPIOx->PUPDR` — pull-up/down if needed

### GPIO Alternate Function
- [ ] `RCC->IOPENR` — enable port clock
- [ ] `GPIOx->MODER` — set to `10` (alternate function)
- [ ] `GPIOx->AFR[0/1]` — set correct AF number (from DS12232 Table 13)
- [ ] `GPIOx->OTYPER`, `OSPEEDR`, `PUPDR` as required by peripheral

### USART (Polling)
- [ ] `RCC->APBENR2` or `APBENR1` — enable USART clock
- [ ] GPIO pins configured as alternate function (TX/RX)
- [ ] `USARTx->BRR` — set baud rate (`SystemCoreClock / baud`)
- [ ] `USARTx->CR1` — set word length, parity, enable TE+RE
- [ ] `USARTx->CR1 |= USART_CR1_UE` — enable USART last

### SPI (Master)
- [ ] `RCC->APBENR2` — enable SPI1 clock
- [ ] GPIO: SCK, MOSI, MISO as alternate function; NSS as GPIO output
- [ ] `SPIx->CR1` — CPOL, CPHA, BR[2:0] (baud divider), MSTR, SSM, SSI
- [ ] `SPIx->CR2` — data size (DS[3:0]), SSOE
- [ ] `SPIx->CR1 |= SPI_CR1_SPE` — enable last

### TIM (Basic Delay)
- [ ] `RCC->APBENR1` — enable TIMx clock
- [ ] `TIMx->PSC` — prescaler (`SystemCoreClock / 1000000 - 1` for 1 µs tick)
- [ ] `TIMx->ARR` — auto-reload value
- [ ] `TIMx->CR1 |= TIM_CR1_CEN` — enable counter

### ADC
- [ ] `RCC->APBENR2` — enable ADC clock
- [ ] GPIO pin to Analog mode (`MODER = 11`)
- [ ] ADC calibration: `ADC1->CR |= ADC_CR_ADCAL`, wait for it to clear
- [ ] `ADC1->CFGR1` — resolution, alignment, continuous/single mode
- [ ] `ADC1->CHSELR` — select input channel
- [ ] `ADC1->CR |= ADC_CR_ADEN` — enable, wait for `ADC_ISR_ADRDY`
- [ ] `ADC1->CR |= ADC_CR_ADSTART` — start conversion
- [ ] Poll `ADC1->ISR & ADC_ISR_EOC`, read `ADC1->DR`

### DMA (any peripheral)
- [ ] `RCC->AHBENR |= RCC_AHBENR_DMA1EN` — clock
- [ ] Clear `DMA_CCR_EN` before configuration
- [ ] `DMAMUX1_Channel0[ch-1].CCR` — set peripheral request ID (RM0444 Table 78)
- [ ] `DMAx_ChannelN->CPAR` — peripheral data register address
- [ ] `DMAx_ChannelN->CMAR` — memory buffer address
- [ ] `DMAx_ChannelN->CNDTR` — transfer count
- [ ] `DMAx_ChannelN->CCR` — direction, sizes, MINC/PINC, mode, interrupts
- [ ] Enable peripheral's DMA request (e.g. `USART1->CR3 |= USART_CR3_DMAT`)
- [ ] Set `DMA_CCR_EN` last

---

## Debugging Tips

**Silent writes:** If a register write has no effect, the clock is probably not enabled. Double-check `RCC->AHBENR`, `APBENR1`, `APBENR2`, and `IOPENR`.

**Hard fault:** Usually a null/invalid pointer dereference, stack overflow, or unaligned memory access. Check that your buffer addresses are valid and that SRAM usage doesn't exceed 36 KB.

**DMA never completes:** Check DMAMUX request ID against Table 78. Verify the peripheral has DMA requests enabled (e.g. `USART_CR3_DMAT`). Check the DMAMUX off-by-one.

**USART garbage output:** Baud rate mismatch — verify `SystemCoreClock` matches your actual clock, and recheck `BRR` calculation.

**ADC reads 0 or 4095 always:** GPIO pin not set to analog mode (`MODER = 11`), or calibration not run before enabling.

**Interrupt never fires:** Peripheral interrupt enable bit not set, OR `NVIC_EnableIRQ()` not called, OR handler name is wrong (check the weak symbol in the startup `.s` file).

---

## Key RM0444 Chapter Reference

| Chapter | Topic |
|---|---|
| 2 | Memory map and register boundary addresses |
| 4 | Power control (PWR) |
| 5 | Reset and clock control (RCC) — clock enables |
| 7 | Flash memory interface |
| 9 | GPIO |
| 10 | DMA controller |
| 11 | DMAMUX request multiplexer |
| 12 | NVIC and SysTick |
| 13 | EXTI — external interrupt/event controller |
| 15 | ADC |
| 16 | DAC |
| 17 | Comparators |
| 18 | Operational amplifiers |
| 21 | Advanced timer TIM1 |
| 22 | General-purpose timers TIM2/TIM3 |
| 24 | Basic timers TIM6/TIM7 |
| 27 | LPTIM (low-power timers) |
| 28 | USART / UART / LPUART |
| 30 | SPI / I2S |
| 32 | I2C |
| 37 | CRC |
| 38 | IWDG / WWDG (watchdogs) |
