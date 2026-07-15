#include <stdint.h>

#include "clock.h"
#include "delay.h"
#include "gpio.h"
#include "uart.h"

int main(void)
{
    heartbeat_init();
    uart_init();

    uart_puts("\n");
    uart_puts("========================================\n");
    uart_puts("STM32F429 SECURITY LAB\n");
    uart_puts("EXP008 CLOCK EXPLORER\n");
    uart_puts("========================================\n");

    clock_probe_init();
    clock_report();

    uart_puts("\nEXP008 READY\n");

    for (;;) {
        heartbeat_toggle();
        uart_puts("heartbeat\n");
        delay_cycles(8000000U);
    }
}
