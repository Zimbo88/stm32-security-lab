#include "delay.h"
void delay_cycles(uint32_t cycles)
{
    volatile uint32_t n = cycles;
    while (n-- != 0U) {
        __asm volatile ("nop");
    }
}
