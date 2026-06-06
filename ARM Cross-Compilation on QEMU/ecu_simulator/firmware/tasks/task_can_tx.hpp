#pragma once
// ─────────────────────────────────────────────────────
//  Task 3: CAN Transmitter
//
//  Drains the sensor queue, builds CAN frames, and
//  transmits them over UART0 in the framed wire format.
//
//  Frame schedule (based on real automotive CAN):
//    0x100 RPM          — every sensor update (100ms)
//    0x101 Throttle     — every sensor update
//    0x200 Coolant temp — every 500ms
//    0x201 Fuel level   — every 1000ms
//    0x202 Battery      — every 1000ms
//    0x7E0 Fault        — immediately on fault change
//
//  Period: 100 ms
//  Priority: 2
// ─────────────────────────────────────────────────────
#include "FreeRTOS.h"
#include "task.h"
#include "../include/ecu_state.hpp"
#include "../include/ecu_protocol.hpp"
#include "../hal/hal_uart.hpp"

namespace Tasks {

class CANTxTask {
public:
    static void run(void* /*param*/) {
        CANTxTask self;
        self.loop();
    }

private:
    uint32_t tick_count_     = 0;
    uint8_t  last_faults_    = 0xFF;  // Force initial fault frame

    void loop() {
        TickType_t last_wake = xTaskGetTickCount();
        for (;;) {
            vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(100));
            ++tick_count_;

            ECU::SensorReading r{};
            // Block briefly for a reading; skip cycle if none ready
            if (xQueueReceive(ECU::g_sensor_queue, &r, pdMS_TO_TICKS(10)) != pdTRUE) {
                continue;
            }

            // Always send RPM + throttle
            send_rpm(r);
            send_throttle(r);

            // Coolant: every 500ms (5 ticks)
            if (tick_count_ % 5 == 0) send_coolant(r);

            // Fuel + battery: every 1000ms (10 ticks)
            if (tick_count_ % 10 == 0) {
                send_fuel(r);
                send_battery(r);
            }

            // Fault frame on any change
            ECUState s = ECU::state_read();
            if (s.active_faults != last_faults_) {
                send_fault(s.active_faults);
                last_faults_ = s.active_faults;
            }
        }
    }

    // ── Frame builders ────────────────────────

    void send_rpm(const ECU::SensorReading& r) {
        CANFrame f{};
        f.sof = FRAME_SOF;
        f.id  = CAN_ID_RPM;
        f.len = 2;
        pack_u16(f.data, r.rpm);
        f.eof = FRAME_EOF;
        transmit(f);
    }

    void send_throttle(const ECU::SensorReading& r) {
        CANFrame f{};
        f.sof    = FRAME_SOF;
        f.id     = CAN_ID_THROTTLE;
        f.len    = 1;
        f.data[0]= r.throttle_pct;
        f.eof    = FRAME_EOF;
        transmit(f);
    }

    void send_coolant(const ECU::SensorReading& r) {
        CANFrame f{};
        f.sof = FRAME_SOF;
        f.id  = CAN_ID_COOLANT_TEMP;
        f.len = 2;
        // Encode as signed 16-bit, scaled ×10 (e.g. 85.5°C → 855)
        int16_t scaled = static_cast<int16_t>(r.coolant_temp_c * 10);
        f.data[0] = static_cast<uint8_t>((scaled >> 8) & 0xFF);
        f.data[1] = static_cast<uint8_t>(scaled & 0xFF);
        f.eof = FRAME_EOF;
        transmit(f);
    }

    void send_fuel(const ECU::SensorReading& r) {
        CANFrame f{};
        f.sof    = FRAME_SOF;
        f.id     = CAN_ID_FUEL_LEVEL;
        f.len    = 1;
        f.data[0]= r.fuel_level_pct;
        f.eof    = FRAME_EOF;
        transmit(f);
    }

    void send_battery(const ECU::SensorReading& r) {
        CANFrame f{};
        f.sof = FRAME_SOF;
        f.id  = CAN_ID_VOLTAGE;
        f.len = 2;
        pack_u16(f.data, r.battery_mv);
        f.eof = FRAME_EOF;
        transmit(f);
    }

    void send_fault(uint8_t fault_mask) {
        ECUState s = ECU::state_read();
        CANFrame f{};
        f.sof    = FRAME_SOF;
        f.id     = CAN_ID_FAULT;
        f.len    = 2;
        f.data[0]= fault_mask;
        f.data[1]= s.engine_state;   // GUI decodes this as engine state
        f.eof    = FRAME_EOF;
        transmit(f);
    }

    // ── Wire transmit ─────────────────────────
    void transmit(const CANFrame& f) {
        // CANFrame is packed — send as raw bytes
        HAL::uart0.write(reinterpret_cast<const uint8_t*>(&f), FRAME_SIZE);
    }
};

inline void can_tx_task_entry(void* p) { CANTxTask::run(p); }

} // namespace Tasks
