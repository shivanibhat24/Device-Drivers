/*
 * uart.c
 *
 *  Created on: Jul 30, 2025
 *      Author: sg78b
 */
#include "uart.h"
#include<stdint.h>

#define UART2EN  (1U<<17)
#define GPIOAEN  (1U<<0)
#define CR1_TE   (1U<<3)
#define CR1_RE   (1U<<2)
#define CR1_UE   (1U<<13)

#define UART_BAUDRATE 115200
#define CLK 16000000

static void uart_set_baudrate(uint32_t periph_clk, uint32_t baudrate);
static uint16_t compute_uart_bd (uint32_t periph_clk, uint32_t baudrate);

void uart2_tx_init(void)
{
	// **** Configure UART GPIO PIN ****
	// 1. Enable clock access to GPIO A
	RCC -> AHB1ENR |= GPIOAEN;
	// 2. Set PA2 mode to alternate function mode
	GPIOAEN -> MODER &= ~(1U<<4);
	GPIOAEN -> MODER |= (1U<<5);
	// 3. Set PA2 alternate function's function type to AF7(UART2_TX)
	GPIOAEN -> AFR[0] |= (1U<<8);
	GPIOAEN -> AFR[0] |= (1U<<9);
	GPIOAEN -> AFR[0] |= (1U<<10);
	GPIOAEN -> AFR[0] &= ~(1U<<11);
	// **** Configure UART Module ****
	// 4. Enable Clock access to UART2
	RCC -> AHB1ENR |= UART2EN;
	// 5. Set Baud Rate
	uart_set_baudrate(CLK,UART_BAUDRATE);
	// 6. Set Transfer Direction
	USART2 -> CR1 = CR1_TE;
	// 7. Enable UART Module
	USART2 -> CR1 |= CR1_UE;
}

static uint16_t compute_uart_bd (uint32_t periph_clk, uint32_t baudrate)
{
	return (periph_clk+(baudrate/2U))/baudrate;
}

static void uart_set_baudrate(uint32_t periph_clk, uint32_t baudrate)
{
	USART2 -> BRR = compute_uart_bd(periph_clk,baudrate);
}
