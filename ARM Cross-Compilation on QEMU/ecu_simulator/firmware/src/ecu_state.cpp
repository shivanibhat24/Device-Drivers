#include "ecu_state.hpp"

namespace ECU {

ECUState         g_state{};
SemaphoreHandle_t g_state_mutex  = nullptr;
QueueHandle_t    g_sensor_queue  = nullptr;
QueueHandle_t    g_fault_queue   = nullptr;
QueueHandle_t    g_control_queue = nullptr;
QueueHandle_t    g_can_tx_queue  = nullptr;

TaskHandle_t     g_task_engine   = nullptr;
TaskHandle_t     g_task_sensors  = nullptr;
TaskHandle_t     g_task_can_tx   = nullptr;
TaskHandle_t     g_task_diag     = nullptr;
TaskHandle_t     g_task_watchdog = nullptr;

void init() {
    g_state_mutex   = xSemaphoreCreateMutex();
    g_sensor_queue  = xQueueCreate(8,  sizeof(SensorReading));
    g_fault_queue   = xQueueCreate(16, sizeof(FaultEvent));
    g_control_queue = xQueueCreate(8,  sizeof(ControlEvent));
    g_can_tx_queue  = xQueueCreate(16, sizeof(CANFrame));

    // Default ECU state: engine off, nominal values
    g_state = ECUState{
        .rpm           = 0,
        .throttle_pct  = 0,
        .coolant_temp_c= 20,
        .fuel_level_pct= 100,
        .battery_mv    = 12600,
        .active_faults = 0,
        .engine_state  = 0,
    };
}

} // namespace ECU
