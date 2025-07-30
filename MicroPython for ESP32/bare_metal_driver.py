import pyb;
import stm;

PERIPH_BASE = 0x40000000
APB1PERIPH_BASE = PERIPH_BASE + 0x00000000
GPIOA_BASE = APB1PERIPH_BASE + 0x00000000
RCC_BASE = PERIPH_BASE + 0x00003800
RCC_APB1ENR = RCC_BASE + 0x30
GPIO_A_MODE_R= GPIOA_BASE + 0x00
GPIO_A_OD_R= GPIOA_BASE + 0x14

GPIOAEN= (1<<0)
PINS=(1<<5)
LED_PIN=PINS

def psudo_delay():
    count=0
    for i in range(100000):
        count+=1

def main():
    stm.mem32[RCC_APB1ENR] = GPIOAEN
    stm.mem32[GPIO_A_MODE_R] = (1<<10)
    stm.mem32[GPIO_A_MODE_R] &=~ (1<<10)
    stm
    while True:
        stm.mem32[GPIO_A_OD_R] ^= LED_PIN
        pseuo_delay(90000)

main()
