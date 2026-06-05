#include "dma_driver.h"
#include "stm32g0xx.h"

#define BUFFER_SIZE 64

uint8_t src_buffer[BUFFER_SIZE];
uint8_t dst_buffer[BUFFER_SIZE];

int main(void) {
    /* Fill source buffer */
    for (int i = 0; i < BUFFER_SIZE; i++) {
        src_buffer[i] = (uint8_t)i;
    }

    DMA_Config cfg = {
        .channel      = 1,                          // Use DMA channel 1
        .direction    = DMA_DIR_MEM_TO_MEM,
        .dmamux_reqid = 0,                           // 0 = no peripheral (M2M)
        .src_addr     = (uint32_t)src_buffer,
        .dst_addr     = (uint32_t)dst_buffer,
        .length       = BUFFER_SIZE,
        .circular     = 0
    };

    DMA_Init(&cfg);
    DMA_Start(1);

    /* Poll for completion (or use interrupt) */
    while (!DMA_IsComplete(1));
    DMA_ClearFlags(1);

    /* dst_buffer now contains a copy of src_buffer */
    while (1) {}
}
