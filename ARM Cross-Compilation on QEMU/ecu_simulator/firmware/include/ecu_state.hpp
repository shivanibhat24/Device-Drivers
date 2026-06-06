#pragma once
// ─────────────────────────────────────────────────────
//  ECU Shared State
//
//  All FreeRTOS tasks read/write ECU state through
//  this module. A single FreeRTOS mutex protects the
//  struct. Queues are used for task-to-task messaging.
// ─────────────────────────────────────────────────────
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "../include/ecu_protocol.hpp"

namespace ECU {

// ── Sensor reading passed from T2 → T3 ───────
struct SensorReading {
    uint32_t timestamp_ms;
    uint16_t rpm;
    uint8_t  throttle_pct;
    int16_t  coolant_temp_c;
    uint8_t  fuel_level_pct;
    uint16_t battery_mv;
};

// ── Fault event passed from any task → T4 ────
struct FaultEvent {
    FaultCode  code;
    uint32_t   timestamp_ms;
    uint8_t    context;        // task-specific detail byte
};

// ── Control command from UART RX → tasks ─────
struct ControlEvent {
    ControlCmd cmd;
    uint8_t    arg[2];         // up to 2 argument bytes
};

// ── Shared state ──────────────────────────────
extern ECUState      g_state;
extern SemaphoreHandle_t g_state_mutex;

// ── Inter-task queues ─────────────────────────
extern QueueHandle_t g_sensor_queue;    // SensorReading   (T2 → T3)
extern QueueHandle_t g_fault_queue;     // FaultEvent      (any → T4)
extern QueueHandle_t g_control_queue;   // ControlEvent    (UART ISR → T1/T2)
extern QueueHandle_t g_can_tx_queue;    // CANFrame        (T3 internal)

// ── Task handles (for watchdog monitoring) ───
extern TaskHandle_t  g_task_engine;
extern TaskHandle_t  g_task_sensors;
extern TaskHandle_t  g_task_can_tx;
extern TaskHandle_t  g_task_diag;
extern TaskHandle_t  g_task_watchdog;

// ── Helpers ───────────────────────────────────

// Thread-safe read of the global ECU state
inline ECUState state_read() {
    ECUState copy;
    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    copy = g_state;
    xSemaphoreGive(g_state_mutex);
    return copy;
}

// Thread-safe write of a single field (use lambda)
// Usage: state_update([](ECUState& s){ s.rpm = 3000; });
template<typename Fn>
void state_update(Fn fn) {
    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    fn(g_state);
    xSemaphoreGive(g_state_mutex);
}

// Post a fault (non-blocking, drops if queue full)
inline void post_fault(FaultCode code, uint8_t ctx = 0) {
    FaultEvent ev{code, static_cast<uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS), ctx};
    xQueueSend(g_fault_queue, &ev, 0);
}

// Initialise all queues and the mutex — called from main()
void init();

} // namespace ECU
