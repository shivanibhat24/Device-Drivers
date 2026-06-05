#ifndef DMA_DRIVER_H
#define DMA_DRIVER_H

#include <stdint.h>
#include <stddef.h>

typedef enum {
    DMA_DIR_MEM_TO_MEM = 0,
    DMA_DIR_MEM_TO_PERIPH,
    DMA_DIR_PERIPH_TO_MEM
} DMA_Direction;

typedef struct {
    uint8_t     channel;        // 1–7
    DMA_Direction direction;
    uint8_t     dmamux_reqid;   // Peripheral request ID (0 = software/mem-to-mem)
    uint32_t    src_addr;
    uint32_t    dst_addr;
    uint16_t    length;
    uint8_t     circular;       // 1 = circular mode
} DMA_Config;

void DMA_Init(const DMA_Config *cfg);
void DMA_Start(uint8_t channel);
void DMA_Stop(uint8_t channel);
uint8_t DMA_IsComplete(uint8_t channel);
void DMA_ClearFlags(uint8_t channel);

#endif
