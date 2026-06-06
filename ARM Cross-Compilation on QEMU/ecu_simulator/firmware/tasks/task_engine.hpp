#pragma once
// ─────────────────────────────────────────────────────
//  Task 1: Engine RPM Simulation
//
//  State machine: OFF → CRANKING → RUNNING → FAULT
//  Physics model:
//    - RPM climbs toward (throttle_pct * 80) + 800 (idle)
//    - Rate limited: +300 rpm/tick up, -200 rpm/tick down
//    - Stall if rpm < 400 while running
//    - Fault state if active_faults != 0
//
//  Period: 50 ms  (20 Hz update)
//  Priority: 3
// ─────────────────────────────────────────────────────
#include "FreeRTOS.h"
#include "task.h"
#include "../include/ecu_state.hpp"
#include "../include/ecu_protocol.hpp"

namespace Tasks {

// Engine state machine
enum class EngineState : uint8_t {
    OFF      = 0,
    CRANKING = 1,
    RUNNING  = 2,
    FAULT    = 3,
};

class EngineTask {
public:
    static void run(void* /*param*/) {
        EngineTask self;
        self.loop();
    }

private:
    EngineState state_     = EngineState::OFF;
    uint16_t    crank_ticks_ = 0;
    uint16_t    rpm_         = 0;

    static constexpr uint16_t IDLE_RPM   = 800;
    static constexpr uint16_t MAX_RPM    = 8000;
    static constexpr uint16_t STALL_RPM  = 350;
    static constexpr uint16_t RAMP_UP    = 300;
    static constexpr uint16_t RAMP_DOWN  = 200;
    static constexpr uint16_t CRANK_TIME = 20;   // ticks @ 50ms = 1 second

    void loop() {
        TickType_t last_wake = xTaskGetTickCount();
        for (;;) {
            vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(50));

            ECUState s = ECU::state_read();
            tick(s);
        }
    }

    void tick(const ECUState& s) {
        // Propagate faults → FAULT state
        if (s.active_faults && state_ == EngineState::RUNNING) {
            transition(EngineState::FAULT);
            return;
        }
        // Clear fault if no faults active
        if (!s.active_faults && state_ == EngineState::FAULT) {
            transition(EngineState::OFF);
            return;
        }

        switch (state_) {
        case EngineState::OFF:
            rpm_ = 0;
            // Auto-start when throttle > 5%
            if (s.throttle_pct > 5) transition(EngineState::CRANKING);
            break;

        case EngineState::CRANKING:
            // Simulate starter motor: slow ramp to idle
            rpm_ += 40;
            if (++crank_ticks_ >= CRANK_TIME) {
                crank_ticks_ = 0;
                transition(EngineState::RUNNING);
            }
            break;

        case EngineState::RUNNING: {
            // Target RPM based on throttle
            uint16_t target = static_cast<uint16_t>(
                IDLE_RPM + (s.throttle_pct * (MAX_RPM - IDLE_RPM)) / 100
            );
            // Rate-limited ramp
            if (rpm_ < target) {
                rpm_ = static_cast<uint16_t>(
                    rpm_ + (rpm_ + RAMP_UP > target ? target - rpm_ : RAMP_UP)
                );
            } else if (rpm_ > target) {
                rpm_ = static_cast<uint16_t>(
                    rpm_ - (rpm_ - RAMP_DOWN < target ? rpm_ - target : RAMP_DOWN)
                );
            }
            // Stall check
            if (rpm_ < STALL_RPM && s.throttle_pct == 0) {
                transition(EngineState::OFF);
            }
            break;
        }

        case EngineState::FAULT:
            // Ramp RPM down quickly
            if (rpm_ > 0) rpm_ = (rpm_ > 200) ? rpm_ - 200 : 0;
            break;
        }

        // Commit RPM and engine state
        ECU::state_update([&](ECUState& st) {
            st.rpm          = rpm_;
            st.engine_state = static_cast<uint8_t>(state_);
        });
    }

    void transition(EngineState next) {
        state_ = next;
        crank_ticks_ = 0;
    }
};

// FreeRTOS task entry point
inline void engine_task_entry(void* p) { EngineTask::run(p); }

} // namespace Tasks
