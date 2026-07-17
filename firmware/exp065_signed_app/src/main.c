#include "uart.h"

int main(void)
{
    uart_init();

    uart_puts("\n");
    uart_puts("========================================\n");
    uart_puts("EXP065 SIGNED APPLICATION\n");
    uart_puts("Secure boot jump successful.\n");
    uart_puts("Image version = 2\n");
    uart_puts("========================================\n");

    for (;;) {
        __asm volatile ("wfi");
    }
}
