#include "boot_result.h"

#include "uart.h"

void boot_result_print(verify_status_t status)
{
    uart_puts("Verification     = ");
    uart_puts(signed_image_status_text(status));
    uart_puts("\n");
}

void boot_result_halt(void)
{
    uart_puts("Application will NOT be started.\n");
    uart_puts("Bootloader halted safely.\n");

    for (;;) {
    }
}
