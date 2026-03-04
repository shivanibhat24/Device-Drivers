#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Application entry point.
 *         Call this from main() after HAL_Init(), SystemClock_Config(),
 *         MX_GPIO_Init(), and MX_USART2_UART_Init() have completed.
 *         This function initialises FreeRTOS objects, creates all tasks,
 *         and starts the scheduler.  It does not return.
 */
void app_main(void);

#ifdef __cplusplus
}
#endif
