#pragma once
// ─────────────────────────────────────────────────────
//  Task 4: Diagnostics
//
//  Receives FaultEvents from the fault queue.
//  Maintains a DTC (Diagnostic Trouble Code) log.
//  Sends DTC frames over UART0 (CAN 0x7E8).
//  Also sends a human-readable debug line over UART1.
//
//  DTC mapping:
//    OVERHEAT     → P0217  (engine coolant over temperature)
//    SENSOR_DISC  → P0197  (engine coolant temp sensor low)
//    VOLTAGE_DROP → P0562  (system voltage low)
//    WATCHDOG     → U0100  (lost communication, watchdog)
//
//  Period: event-driven (blocks on queue)
//  Priority: 1
// ─────────────────────────────────────────────────────
#include "FreeRTOS.h"
#include "task.h"
#include "../include/ecu_state.hpp"
#include "../include/ecu_protocol.hpp"
#include "../hal/hal_uart.hpp"
#include <cstdio>
#include <cstring>

namespace Tasks {

struct DTC {
    uint16_t code;        // e.g. 0x0217 for P0217
    char     label[24];
    uint32_t first_seen_ms;
    uint32_t last_seen_ms;
    uint8_t  count;
    bool     active;
};

class DiagTask {
public:
    static void run(void* /*param*/) {
        DiagTask self;
        self.loop();
    }

private:
    static constexpr size_t MAX_DTCS = 8;
    DTC dtc_log_[MAX_DTCS]{};
    size_t dtc_count_ = 0;

    void loop() {
        ECU::FaultEvent ev{};
        for (;;) {
            // Block until a fault event arrives (up to 500ms, then heartbeat)
            if (xQueueReceive(ECU::g_fault_queue, &ev, pdMS_TO_TICKS(500)) == pdTRUE) {
                process_fault(ev);
            }
            // Heartbeat DTC summary on UART1 every ~500ms
            send_summary();
        }
    }

    void process_fault(const ECU::FaultEvent& ev) {
        uint16_t dtc_code = fault_to_dtc(ev.code);
        const char* label = fault_label(ev.code);

        // Find existing DTC or create new entry
        DTC* entry = find_or_create(dtc_code, label);
        if (!entry) return;  // log full

        entry->last_seen_ms = ev.timestamp_ms;
        entry->active = true;
        entry->count++;
        if (entry->first_seen_ms == 0) entry->first_seen_ms = ev.timestamp_ms;

        // Emit DTC CAN frame
        send_dtc_frame(dtc_code, entry->count);

        // Debug log to UART1
        char buf[64];
        snprintf(buf, sizeof(buf), "[DIAG] DTC P%04X (%s) count=%u ctx=%02X\r\n",
                 dtc_code, label, entry->count, ev.context);
        HAL::uart1.write_str(buf);
    }

    void send_summary() {
        char buf[80];
        snprintf(buf, sizeof(buf), "[DIAG] DTCs active=%u total=%u\r\n",
                 active_count(), static_cast<unsigned>(dtc_count_));
        HAL::uart1.write_str(buf);
    }

    void send_dtc_frame(uint16_t code, uint8_t count) {
        CANFrame f{};
        f.sof    = FRAME_SOF;
        f.id     = CAN_ID_DTC;
        f.len    = 4;
        pack_u16(f.data, code);
        f.data[2] = count;
        f.data[3] = 0x01;  // confirmed fault
        f.eof    = FRAME_EOF;
        HAL::uart0.write(reinterpret_cast<const uint8_t*>(&f), FRAME_SIZE);
    }

    // ── Helpers ───────────────────────────────

    static uint16_t fault_to_dtc(FaultCode c) {
        switch (c) {
        case FaultCode::OVERHEAT:     return 0x0217;
        case FaultCode::SENSOR_DISC:  return 0x0197;
        case FaultCode::VOLTAGE_DROP: return 0x0562;
        case FaultCode::WATCHDOG_RESET: return 0x0100;
        default: return 0x0000;
        }
    }

    static const char* fault_label(FaultCode c) {
        switch (c) {
        case FaultCode::OVERHEAT:       return "P0217 coolant over-temp";
        case FaultCode::SENSOR_DISC:    return "P0197 sensor low";
        case FaultCode::VOLTAGE_DROP:   return "P0562 sys voltage low";
        case FaultCode::WATCHDOG_RESET: return "U0100 watchdog reset";
        default:                        return "unknown";
        }
    }

    DTC* find_or_create(uint16_t code, const char* label) {
        for (size_t i = 0; i < dtc_count_; i++) {
            if (dtc_log_[i].code == code) return &dtc_log_[i];
        }
        if (dtc_count_ >= MAX_DTCS) return nullptr;
        DTC& d = dtc_log_[dtc_count_++];
        d.code  = code;
        d.count = 0;
        d.first_seen_ms = 0;
        d.active = false;
        strncpy(d.label, label, sizeof(d.label) - 1);
        return &d;
    }

    unsigned active_count() const {
        unsigned n = 0;
        for (size_t i = 0; i < dtc_count_; i++) n += dtc_log_[i].active ? 1 : 0;
        return n;
    }
};

inline void diag_task_entry(void* p) { DiagTask::run(p); }

} // namespace Tasks
