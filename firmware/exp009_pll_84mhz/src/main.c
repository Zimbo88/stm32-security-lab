#include <stdint.h>

#include "clock.h"
#include "delay.h"
#include "gpio.h"
#include "uart.h"

int main(void)
{
    heartbeat_init();

    if (clock_init_84mhz() == 0U) {
        /* Fallback UART on reset-default 16 MHz. */
        uart_init(16000000UL, 115200UL);
        uart_puts("\nCLOCK INIT FAILED\n");

        for (;;) {
            heartbeat_toggle();
            delay_cycles(2000000U);
        }
    }

    uart_init(clock_get_apb2_hz(), 115200UL);

    uart_puts("\n");
    uart_puts("========================================\n");
    uart_puts("STM32F429 SECURITY LAB\n");
    uart_puts("EXP009 PLL 84 MHz\n");
    uart_puts("========================================\n");

    clock_report();

    uart_puts("\nEXP009 READY\n");

    for (;;) {
        heartbeat_toggle();
        uart_puts("heartbeat\n");
        delay_cycles(42000000U);
    }
}
