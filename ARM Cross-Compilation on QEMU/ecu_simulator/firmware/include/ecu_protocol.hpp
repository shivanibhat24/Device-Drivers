#pragma once
#include <cstdint>

// ─────────────────────────────────────────────
//  ECU Simulator — Shared Protocol Definitions
//  Used by both firmware (QEMU) and Qt GUI.
// ─────────────────────────────────────────────

// ── CAN frame IDs ────────────────────────────
constexpr uint32_t CAN_ID_RPM          = 0x100;
constexpr uint32_t CAN_ID_THROTTLE     = 0x101;
constexpr uint32_t CAN_ID_COOLANT_TEMP = 0x200;
constexpr uint32_t CAN_ID_FUEL_LEVEL   = 0x201;
constexpr uint32_t CAN_ID_VOLTAGE      = 0x202;
constexpr uint32_t CAN_ID_FAULT        = 0x7E0;   // OBD-II diagnostic range
// Fault frame payload: data[0]=fault_mask, data[1]=engine_state
constexpr uint32_t CAN_ID_DTC          = 0x7E8;

// ── CAN frame over UART ───────────────────────
// Wire format (10 bytes fixed):
//   [0xAA] [ID_HI] [ID_LO] [LEN] [D0..D7 up to 8] ... [0x55]
// We always send 12 bytes total for simplicity:
//   SOF(1) + ID(2) + LEN(1) + DATA(8) + EOF(1) = 13 bytes
constexpr uint8_t  FRAME_SOF = 0xAA;
constexpr uint8_t  FRAME_EOF = 0x55;
constexpr uint16_t FRAME_SIZE = 13;

struct __attribute__((packed)) CANFrame {
    uint8_t  sof;           // Always FRAME_SOF
    uint16_t id;            // CAN arbitration ID
    uint8_t  len;           // Data length (0-8)
    uint8_t  data[8];       // Payload
    uint8_t  eof;           // Always FRAME_EOF
};

// ── TCP control commands (Qt → firmware) ─────
// Single-byte command codes sent over TCP :5000
enum class ControlCmd : uint8_t {
    SET_THROTTLE      = 0x01,  // + 1 byte value 0-100
    INJECT_OVERHEAT   = 0x10,
    INJECT_SENSOR_DISC= 0x11,
    INJECT_VOLT_DROP  = 0x12,
    CLEAR_FAULTS      = 0x20,
    SET_RPM_TARGET    = 0x30,  // + 2 byte uint16 RPM
    PING              = 0xFF,
};

// ── Fault codes ───────────────────────────────
enum class FaultCode : uint8_t {
    NONE             = 0x00,
    OVERHEAT         = 0x01,   // DTC P0217
    SENSOR_DISC      = 0x02,   // DTC P0197
    VOLTAGE_DROP     = 0x04,   // DTC P0562
    WATCHDOG_RESET   = 0x08,
};

// ── ECU state (firmware internal + telemetry) ─
struct ECUState {
    uint16_t rpm;              // 0 – 8000
    uint8_t  throttle_pct;     // 0 – 100
    int16_t  coolant_temp_c;   // -40 – 150 °C
    uint8_t  fuel_level_pct;   // 0 – 100
    uint16_t battery_mv;       // millivolts, nominal 12000
    uint8_t  active_faults;    // bitmask of FaultCode
    uint8_t  engine_state;     // 0=off 1=cranking 2=running 3=fault
};

// ── Helper: pack/unpack uint16 big-endian ─────
inline void pack_u16(uint8_t* buf, uint16_t v) {
    buf[0] = static_cast<uint8_t>(v >> 8);
    buf[1] = static_cast<uint8_t>(v & 0xFF);
}
inline uint16_t unpack_u16(const uint8_t* buf) {
    return static_cast<uint16_t>((buf[0] << 8) | buf[1]);
}
