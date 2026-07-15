#include "delay.h"

void delay_cycles(uint32_t cycles)
{
    volatile uint32_t remaining = cycles;

    while (remaining != 0U) {
        __asm volatile ("nop");
        --remaining;
    }
}
