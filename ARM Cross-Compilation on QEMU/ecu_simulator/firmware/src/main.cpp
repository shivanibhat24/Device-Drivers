// ─────────────────────────────────────────────────────
//  ECU Simulator Firmware — main.cpp
//  Target: QEMU sifive_u (RISC-V 64-bit)
//  RTOS:   FreeRTOS
//
//  Boot sequence:
//    1. Init UART0 (CAN frames) + UART1 (control/debug)
//    2. Init ECU shared state and queues
//    3. Start UART1 control receiver task
//    4. Create all 5 ECU tasks
//    5. Start FreeRTOS scheduler (never returns)
// ─────────────────────────────────────────────────────
#include <cstdio>

#include "FreeRTOS.h"
#include "task.h"

#include "include/ecu_state.hpp"
#include "hal/hal_uart.hpp"
#include "tasks/task_engine.hpp"
#include "tasks/task_sensors.hpp"
#include "tasks/task_can_tx.hpp"
#include "tasks/task_diag.hpp"
#include "tasks/task_watchdog.hpp"

// ── UART1 control receiver ────────────────────
// Reads ControlCmd packets from UART1 (TCP bridge)
// and enqueues them for task_sensors to process.
static void uart_ctrl_task(void* /*param*/) {
    enum class RxState { IDLE, GOT_CMD };
    RxState  state = RxState::IDLE;
    ECU::ControlEvent ev{};
    uint8_t  arg_idx = 0;
    uint8_t  args_needed = 0;

    auto args_for = [](ControlCmd c) -> uint8_t {
        switch (c) {
        case ControlCmd::SET_THROTTLE:   return 1;
        case ControlCmd::SET_RPM_TARGET: return 2;
        default:                         return 0;
        }
    };

    for (;;) {
        uint8_t b;
        if (!HAL::uart1.read_byte(b)) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        switch (state) {
        case RxState::IDLE:
            ev.cmd     = static_cast<ControlCmd>(b);
            ev.arg[0]  = 0;
            ev.arg[1]  = 0;
            arg_idx    = 0;
            args_needed = args_for(ev.cmd);
            if (args_needed == 0) {
                xQueueSend(ECU::g_control_queue, &ev, 0);
            } else {
                state = RxState::GOT_CMD;
            }
            break;

        case RxState::GOT_CMD:
            ev.arg[arg_idx++] = b;
            if (arg_idx >= args_needed) {
                xQueueSend(ECU::g_control_queue, &ev, 0);
                state = RxState::IDLE;
            }
            break;
        }
    }
}

// ── FreeRTOS stack sizes ──────────────────────
// sifive_u has plenty of RAM for simulation
constexpr uint32_t STACK_ENGINE   = 512;
constexpr uint32_t STACK_SENSORS  = 512;
constexpr uint32_t STACK_CAN_TX   = 512;
constexpr uint32_t STACK_DIAG     = 512;
constexpr uint32_t STACK_WATCHDOG = 256;
constexpr uint32_t STACK_UART_RX  = 256;

// ── Task priorities ───────────────────────────
// Higher number = higher priority in FreeRTOS
constexpr UBaseType_t PRI_WATCHDOG = 4;
constexpr UBaseType_t PRI_ENGINE   = 3;
constexpr UBaseType_t PRI_SENSORS  = 3;
constexpr UBaseType_t PRI_CAN_TX   = 2;
constexpr UBaseType_t PRI_DIAG     = 1;
constexpr UBaseType_t PRI_UART_RX  = 2;

extern "C" int main() {
    // 1. Initialise UARTs
    HAL::uart0.init(115200);   // CAN frames
    HAL::uart1.init(115200);   // Control/debug

    HAL::uart1.write_str("[ECU] Booting...\r\n");

    // 2. Initialise ECU shared state + queues
    ECU::init();

    HAL::uart1.write_str("[ECU] Queues ready\r\n");

    // 3. Create tasks
    xTaskCreate(Tasks::engine_task_entry,   "Engine",   STACK_ENGINE,   nullptr, PRI_ENGINE,   &ECU::g_task_engine);
    xTaskCreate(Tasks::sensor_task_entry,   "Sensors",  STACK_SENSORS,  nullptr, PRI_SENSORS,  &ECU::g_task_sensors);
    xTaskCreate(Tasks::can_tx_task_entry,   "CAN_TX",   STACK_CAN_TX,   nullptr, PRI_CAN_TX,   &ECU::g_task_can_tx);
    xTaskCreate(Tasks::diag_task_entry,     "Diag",     STACK_DIAG,     nullptr, PRI_DIAG,     &ECU::g_task_diag);
    xTaskCreate(Tasks::watchdog_task_entry, "Watchdog", STACK_WATCHDOG, nullptr, PRI_WATCHDOG, &ECU::g_task_watchdog);
    xTaskCreate(uart_ctrl_task,             "UartRx",   STACK_UART_RX,  nullptr, PRI_UART_RX,  nullptr);

    HAL::uart1.write_str("[ECU] Scheduler starting\r\n");

    // 4. Start scheduler — does not return
    vTaskStartScheduler();

    // Should never reach here
    for (;;) {}
}

// ── FreeRTOS hooks ────────────────────────────
extern "C" void vApplicationIdleHook() {
    // Can put processor in WFI here for power saving
}

extern "C" void vApplicationStackOverflowHook(TaskHandle_t /*task*/, char* name) {
    char buf[48];
    snprintf(buf, sizeof(buf), "[FATAL] Stack overflow in task: %s\r\n", name);
    HAL::uart1.write_str(buf);
    for (;;) {}
}

extern "C" void vApplicationMallocFailedHook() {
    HAL::uart1.write_str("[FATAL] Malloc failed\r\n");
    for (;;) {}
}
