#include "boot_info.h"

#include "signed_image.h"
#include "uart.h"

void boot_info_print_banner(void)
{
    uart_puts("\n========================================\n");
    uart_puts("STM32F429 SECURITY LAB\n");
    uart_puts("EXP045 BOOTLOADER V2\n");
    uart_puts("========================================\n");
}

void boot_info_print_manifest(void)
{
    const signed_manifest_t *m =
        (const signed_manifest_t *)SIGNED_IMAGE_BASE;

    uart_puts("Manifest address = ");
    uart_put_hex32(SIGNED_IMAGE_BASE);

    uart_puts("\nMagic            = ");
    uart_put_hex32(m->magic);

    uart_puts("\nHeader version   = ");
    uart_put_u32(m->header_version);

    uart_puts("\nImage version    = ");
    uart_put_u32(m->image_version);

    uart_puts("\nMinimum version  = ");
    uart_put_u32(MIN_IMAGE_VERSION);

    uart_puts("\nVector address   = ");
    uart_put_hex32(m->vector_address);

    uart_puts("\nImage size       = ");
    uart_put_u32(m->image_size);
    uart_puts(" bytes\n");
}
