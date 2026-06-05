#include "dma_driver.h"
#include "stm32g0xx.h"  // CMSIS device header — provides DMA1, DMAMUX1, RCC

void DMA_Init(const DMA_Config *cfg) {
    uint8_t ch = cfg->channel;  // 1-based (1–7)

    /* 1. Enable DMA1 and DMAMUX clocks */
    RCC->AHBENR |= RCC_AHBENR_DMA1EN;    // DMA1 clock
    // DMAMUX clock is enabled automatically with DMA1 on G0

    /* 2. Select DMA channel pointer (channels are 1-indexed in HAL but
          the array is 0-indexed: DMA1_Channel1 = DMA1->Channel[0]) */
    DMA_Channel_TypeDef *dma_ch = (DMA_Channel_TypeDef *)(
        DMA1_BASE + 0x08 + ((ch - 1) * 0x14)
    );

    /* 3. Disable channel before configuration */
    dma_ch->CCR &= ~DMA_CCR_EN;

    /* 4. Configure DMAMUX — channel index is 0-based (ch-1)!  */
    DMAMUX1_Channel0[ch - 1].CCR = (cfg->dmamux_reqid & 0x7F);

    /* 5. Set peripheral and memory addresses */
    dma_ch->CPAR = cfg->src_addr;   // Peripheral (or source) address
    dma_ch->CMAR = cfg->dst_addr;   // Memory (destination) address

    /* 6. Set transfer count */
    dma_ch->CNDTR = cfg->length;

    /* 7. Build CCR configuration word */
    uint32_t ccr = 0;

    if (cfg->direction == DMA_DIR_MEM_TO_MEM) {
        ccr |= DMA_CCR_MEM2MEM;      // Memory-to-memory mode
        ccr |= DMA_CCR_MINC;         // Increment memory address
        ccr |= DMA_CCR_PINC;         // Increment peripheral (source) address
        ccr |= DMA_CCR_DIR;          // Read from peripheral (source)
    } else if (cfg->direction == DMA_DIR_MEM_TO_PERIPH) {
        ccr |= DMA_CCR_DIR;          // Read from memory, write to peripheral
        ccr |= DMA_CCR_MINC;         // Increment memory address
    } else {  // PERIPH_TO_MEM
        ccr |= DMA_CCR_MINC;         // Increment memory destination
    }

    /* Data size: 00=byte, 01=half-word, 10=word */
    ccr |= (0 << DMA_CCR_MSIZE_Pos); // 8-bit memory transfers
    ccr |= (0 << DMA_CCR_PSIZE_Pos); // 8-bit peripheral transfers

    if (cfg->circular) {
        ccr |= DMA_CCR_CIRC;         // Circular mode
    }

    ccr |= DMA_CCR_TCIE;             // Transfer complete interrupt enable
    ccr |= DMA_CCR_TEIE;             // Transfer error interrupt enable

    dma_ch->CCR = ccr;
}

void DMA_Start(uint8_t channel) {
    DMA_Channel_TypeDef *dma_ch = (DMA_Channel_TypeDef *)(
        DMA1_BASE + 0x08 + ((channel - 1) * 0x14)
    );
    dma_ch->CCR |= DMA_CCR_EN;
}

void DMA_Stop(uint8_t channel) {
    DMA_Channel_TypeDef *dma_ch = (DMA_Channel_TypeDef *)(
        DMA1_BASE + 0x08 + ((channel - 1) * 0x14)
    );
    dma_ch->CCR &= ~DMA_CCR_EN;
}

uint8_t DMA_IsComplete(uint8_t channel) {
    /* ISR flags: each channel occupies 4 bits starting at bit (ch-1)*4 */
    uint32_t tcif_bit = DMA_ISR_TCIF1 << ((channel - 1) * 4);
    return (DMA1->ISR & tcif_bit) ? 1 : 0;
}

void DMA_ClearFlags(uint8_t channel) {
    uint32_t clear_bits = (DMA_IFCR_CGIF1) << ((channel - 1) * 4);
    DMA1->IFCR |= clear_bits;
}
