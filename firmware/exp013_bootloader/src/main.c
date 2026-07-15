#include <stdint.h>

#include "boot.h"
#include "delay.h"
#include "uart.h"

int main(void)
{
    uart_init_115200_hsi16();

    uart_puts("\n");
    uart_puts("========================================\n");
    uart_puts("STM32F429 SECURITY LAB\n");
    uart_puts("EXP013 BOOTLOADER STAGE 1\n");
    uart_puts("========================================\n");

    uart_puts("Application base = ");
    uart_put_hex32(APP_BASE);
    uart_puts("\n");

    uart_puts("Initial MSP      = ");
    uart_put_hex32(*(volatile const uint32_t *)(APP_BASE + 0U));
    uart_puts("\n");

    uart_puts("Reset vector     = ");
    uart_put_hex32(*(volatile const uint32_t *)(APP_BASE + 4U));
    uart_puts("\n");

    if (boot_application_is_valid() == 0U) {
        uart_puts("Application validation: FAIL\n");
        uart_puts("Bootloader halted; flash application at 0x08008000.\n");
        for (;;) {
        }
    }

    uart_puts("Application validation: PASS\n");
    uart_puts("Jumping in approximately one second...\n");
    delay_cycles(4000000U);

    boot_jump_to_application();

    for (;;) {
    }
}
