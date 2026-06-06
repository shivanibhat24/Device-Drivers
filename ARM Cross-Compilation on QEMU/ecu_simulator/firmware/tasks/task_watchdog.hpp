#pragma once
// ─────────────────────────────────────────────────────
//  Task 5: Watchdog Monitor
//
//  Each supervised task calls watchdog_checkin(id)
//  every cycle. The watchdog task checks all IDs
//  within a window. If any task misses too many
//  windows, it's declared hung:
//    - Posts WATCHDOG_RESET fault
//    - Sends debug message on UART1
//    - Attempts vTaskResume (can't fix a deadlock,
//      but documents the hang)
//
//  Pattern: software watchdog inside FreeRTOS.
//  For production you would also kick a hardware WDT.
//
//  Period: 500 ms
//  Priority: 4  (highest — must always run)
// ─────────────────────────────────────────────────────
#include "FreeRTOS.h"
#include "task.h"
#include "../include/ecu_state.hpp"
#include "../include/ecu_protocol.hpp"
#include "../hal/hal_uart.hpp"
#include <cstdio>

namespace Tasks {

// Task IDs supervised by the watchdog
enum class WatchID : uint8_t {
    ENGINE  = 0,
    SENSORS = 1,
    CAN_TX  = 2,
    DIAG    = 3,
    COUNT   = 4,
};

// Called by each task to indicate it's alive
inline volatile uint8_t g_watchdog_counters[static_cast<uint8_t>(WatchID::COUNT)] = {};

inline void watchdog_checkin(WatchID id) {
    g_watchdog_counters[static_cast<uint8_t>(id)]++;
}

class WatchdogTask {
public:
    static void run(void* /*param*/) {
        WatchdogTask self;
        self.loop();
    }

private:
    uint8_t last_counters_[static_cast<uint8_t>(WatchID::COUNT)] = {};
    uint8_t missed_[static_cast<uint8_t>(WatchID::COUNT)]        = {};

    static constexpr uint8_t MAX_MISSED = 3;

    const char* task_names_[static_cast<uint8_t>(WatchID::COUNT)] = {
        "Engine", "Sensors", "CAN_TX", "Diag"
    };

    TaskHandle_t* task_handles_[static_cast<uint8_t>(WatchID::COUNT)] = {
        &ECU::g_task_engine, &ECU::g_task_sensors,
        &ECU::g_task_can_tx, &ECU::g_task_diag,
    };

    void loop() {
        TickType_t last_wake = xTaskGetTickCount();
        for (;;) {
            vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(500));
            check_all();
        }
    }

    void check_all() {
        for (uint8_t i = 0; i < static_cast<uint8_t>(WatchID::COUNT); i++) {
            uint8_t cur = g_watchdog_counters[i];
            if (cur == last_counters_[i]) {
                // Task hasn't checked in
                missed_[i]++;
                if (missed_[i] >= MAX_MISSED) {
                    report_hang(i);
                }
            } else {
                missed_[i] = 0;
            }
            last_counters_[i] = cur;
        }
    }

    void report_hang(uint8_t idx) {
        char buf[64];
        snprintf(buf, sizeof(buf),
                 "[WDG] TASK HUNG: %s (id=%u) — posting fault\r\n",
                 task_names_[idx], idx);
        HAL::uart1.write_str(buf);

        ECU::post_fault(FaultCode::WATCHDOG_RESET, idx);

        // Attempt resume (won't fix deadlocks, but helps runaway sleeps)
        if (*task_handles_[idx]) {
            vTaskResume(*task_handles_[idx]);
        }

        missed_[idx] = 0;  // reset so we don't flood
    }
};

inline void watchdog_task_entry(void* p) { WatchdogTask::run(p); }

} // namespace Tasks
