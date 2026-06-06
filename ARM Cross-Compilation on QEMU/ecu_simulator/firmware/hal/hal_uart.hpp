#pragma once
// ─────────────────────────────────────────────────────
//  HAL: UART — QEMU sifive_u (RISC-V)
//
//  QEMU sifive_u maps UART0 at 0x10013000.
//  The SiFive UART has a simple register layout:
//    +0x00  txdata   bit31=full, bits[7:0]=tx char
//    +0x04  rxdata   bit31=empty, bits[7:0]=rx char
//    +0x08  txctrl   bit0=txen, bit1=nstop, bits[18:16]=txcnt watermark
//    +0x0C  rxctrl   bit0=rxen, bits[18:16]=rxcnt watermark
//    +0x10  ie       bit0=txwm, bit1=rxwm
//    +0x14  ip       bit0=txwm, bit1=rxwm
//    +0x18  div      divisor = (tlclk / baud) - 1
// ─────────────────────────────────────────────────────
#include <cstdint>

namespace HAL {

struct UARTRegs {
    volatile uint32_t txdata;
    volatile uint32_t rxdata;
    volatile uint32_t txctrl;
    volatile uint32_t rxctrl;
    volatile uint32_t ie;
    volatile uint32_t ip;
    volatile uint32_t div;
};

// QEMU sifive_u UART base addresses
constexpr uintptr_t UART0_BASE = 0x10013000;
constexpr uintptr_t UART1_BASE = 0x10023000;

// tlclk assumed 500 MHz for sifive_u in QEMU
constexpr uint32_t TLCLK_HZ   = 500'000'000;

class UART {
public:
    explicit UART(uintptr_t base) : regs_(reinterpret_cast<UARTRegs*>(base)) {}

    void init(uint32_t baud = 115200) {
        regs_->div    = (TLCLK_HZ / baud) - 1;
        regs_->txctrl = 0x1;   // txen
        regs_->rxctrl = 0x1;   // rxen
    }

    // Blocking transmit — waits if TX FIFO full
    void write_byte(uint8_t c) {
        while (regs_->txdata & 0x80000000u) { /* spin */ }
        regs_->txdata = c;
    }

    void write(const uint8_t* buf, uint32_t len) {
        for (uint32_t i = 0; i < len; i++) write_byte(buf[i]);
    }

    void write_str(const char* s) {
        while (*s) write_byte(static_cast<uint8_t>(*s++));
    }

    // Non-blocking receive: returns false if FIFO empty
    bool read_byte(uint8_t& out) {
        uint32_t v = regs_->rxdata;
        if (v & 0x80000000u) return false;
        out = static_cast<uint8_t>(v & 0xFF);
        return true;
    }

private:
    UARTRegs* regs_;
};

// Global instances — defined in hal_uart.cpp
extern UART uart0;  // CAN frames + debug out
extern UART uart1;  // TCP control channel (QEMU second serial)

} // namespace HAL
