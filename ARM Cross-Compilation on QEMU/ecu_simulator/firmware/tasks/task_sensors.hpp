#pragma once
// ─────────────────────────────────────────────────────
//  Task 2: Sensor Manager
//
//  Simulates:
//    - Coolant temp: rises with RPM, cools at idle
//    - Fuel level:   slowly drains with RPM
//    - Battery:      nominal 12.6V, drops under load
//
//  Fault injection (via ControlEvent):
//    INJECT_OVERHEAT   → force coolant_temp > 120°C
//    INJECT_SENSOR_DISC→ mark sensor as disconnected
//    INJECT_VOLT_DROP  → force battery_mv < 10500
//
//  Period: 100 ms  (10 Hz)
//  Priority: 3
// ─────────────────────────────────────────────────────
#include "FreeRTOS.h"
#include "task.h"
#include "../include/ecu_state.hpp"
#include "../include/ecu_protocol.hpp"

namespace Tasks {

class SensorTask {
public:
    static void run(void* /*param*/) {
        SensorTask self;
        self.loop();
    }

private:
    // Simulated physical state (not directly the ECU values)
    float coolant_f_    = 20.0f;   // °C, floating for smooth simulation
    float fuel_f_       = 100.0f;  // %
    float battery_f_    = 12600.f; // mV

    bool  fault_overheat_   = false;
    bool  fault_sensor_disc_= false;
    bool  fault_volt_drop_  = false;

    void loop() {
        TickType_t last_wake = xTaskGetTickCount();
        for (;;) {
            vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(100));
            process_commands();
            simulate();
            check_thresholds();
            publish();
        }
    }

    void process_commands() {
        ECU::ControlEvent ev;
        // Drain the queue of pending control commands
        while (xQueueReceive(ECU::g_control_queue, &ev, 0) == pdTRUE) {
            switch (ev.cmd) {
            case ControlCmd::INJECT_OVERHEAT:
                fault_overheat_    = true;
                break;
            case ControlCmd::INJECT_SENSOR_DISC:
                fault_sensor_disc_ = true;
                break;
            case ControlCmd::INJECT_VOLT_DROP:
                fault_volt_drop_   = true;
                break;
            case ControlCmd::CLEAR_FAULTS:
                fault_overheat_    = false;
                fault_sensor_disc_ = false;
                fault_volt_drop_   = false;
                ECU::state_update([](ECUState& s){ s.active_faults = 0; });
                break;
            case ControlCmd::SET_THROTTLE: {
                uint8_t pct = ev.arg[0];
                if (pct > 100) pct = 100;
                ECU::state_update([pct](ECUState& s){ s.throttle_pct = pct; });
                break;
            }
            default: break;
            }
        }
    }

    void simulate() {
        ECUState s = ECU::state_read();
        float rpm_norm = s.rpm / 8000.0f;  // 0..1

        // ── Coolant temperature ──────────────────
        if (fault_overheat_) {
            coolant_f_ = coolant_f_ < 125.0f ? coolant_f_ + 2.0f : 130.0f;
        } else {
            // Rises proportional to RPM, cools toward ambient (20°C)
            float target = 20.0f + rpm_norm * 85.0f;  // max 105°C at WOT
            coolant_f_ += (target - coolant_f_) * 0.03f;
        }

        // ── Fuel level ───────────────────────────
        if (!fault_sensor_disc_) {
            // Drain: 0.005% per tick per 1000 RPM
            fuel_f_ -= rpm_norm * 0.005f;
            if (fuel_f_ < 0.0f) fuel_f_ = 0.0f;
        }

        // ── Battery voltage ──────────────────────
        if (fault_volt_drop_) {
            battery_f_ = battery_f_ > 9500.f ? battery_f_ - 50.f : 9000.f;
        } else {
            // Sags slightly under load, recovers at idle
            float target = 12600.f - rpm_norm * 300.f;
            battery_f_ += (target - battery_f_) * 0.05f;
        }
    }

    void check_thresholds() {
        ECUState s = ECU::state_read();
        uint8_t new_faults = s.active_faults;

        if (coolant_f_ > 120.0f) {
            new_faults |= static_cast<uint8_t>(FaultCode::OVERHEAT);
            ECU::post_fault(FaultCode::OVERHEAT, static_cast<uint8_t>(coolant_f_));
        }
        if (fault_sensor_disc_) {
            new_faults |= static_cast<uint8_t>(FaultCode::SENSOR_DISC);
            ECU::post_fault(FaultCode::SENSOR_DISC);
        }
        if (battery_f_ < 10500.f) {
            new_faults |= static_cast<uint8_t>(FaultCode::VOLTAGE_DROP);
            ECU::post_fault(FaultCode::VOLTAGE_DROP, static_cast<uint8_t>(battery_f_ / 100));
        }

        ECU::state_update([&](ECUState& st) {
            st.coolant_temp_c  = static_cast<int16_t>(coolant_f_);
            st.fuel_level_pct  = static_cast<uint8_t>(fuel_f_);
            st.battery_mv      = static_cast<uint16_t>(battery_f_);
            st.active_faults   = new_faults;
        });
    }

    void publish() {
        ECUState s = ECU::state_read();
        ECU::SensorReading r{};
        r.timestamp_ms   = xTaskGetTickCount() * portTICK_PERIOD_MS;
        r.rpm            = s.rpm;
        r.throttle_pct   = s.throttle_pct;
        r.coolant_temp_c = s.coolant_temp_c;
        r.fuel_level_pct = fault_sensor_disc_ ? 0xFF : s.fuel_level_pct;
        r.battery_mv     = s.battery_mv;
        // Non-blocking send — CAN TX task drains this
        xQueueSend(ECU::g_sensor_queue, &r, 0);
    }
};

inline void sensor_task_entry(void* p) { SensorTask::run(p); }

} // namespace Tasks
