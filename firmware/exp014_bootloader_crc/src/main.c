#include <stdint.h>

#include "delay.h"
#include "image.h"
#include "uart.h"

int main(void)
{
    const image_header_t *h =
        (const image_header_t *)IMAGE_HEADER_ADDRESS;
    uint32_t computed_crc = 0U;

    uart_init_115200_hsi16();

    uart_puts("\n========================================\n");
    uart_puts("STM32F429 SECURITY LAB\n");
    uart_puts("EXP014 IMAGE HEADER + CRC32\n");
    uart_puts("========================================\n");

    uart_puts("Header address   = ");
    uart_put_hex32(IMAGE_HEADER_ADDRESS);
    uart_puts("\nMagic            = ");
    uart_put_hex32(h->magic);
    uart_puts("\nHeader version   = ");
    uart_put_u32(h->header_version);
    uart_puts("\nImage version    = ");
    uart_put_u32(h->image_version);
    uart_puts("\nVector address   = ");
    uart_put_hex32(h->vector_address);
    uart_puts("\nImage size       = ");
    uart_put_u32(h->image_size);
    uart_puts(" bytes\nStored CRC32     = ");
    uart_put_hex32(h->image_crc32);
    uart_puts("\n");

    const image_status_t status = image_validate(&computed_crc);

    uart_puts("Computed CRC32   = ");
    uart_put_hex32(computed_crc);
    uart_puts("\nValidation       = ");
    uart_puts(image_status_text(status));
    uart_puts("\n");

    if (status != IMAGE_OK) {
        uart_puts("Application will NOT be started.\n");
        uart_puts("Bootloader halted safely.\n");
        for (;;) {
        }
    }

    uart_puts("Integrity check passed.\n");
    uart_puts("Jumping to application...\n");
    delay_cycles(4000000U);

    image_jump();

    for (;;) {
    }
}
