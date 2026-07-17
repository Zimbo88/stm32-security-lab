#include <stdint.h>

#include "delay.h"
#include "boot_policy.h"
#include "signed_image.h"
#include "uart.h"

int main(void)
{
    const signed_manifest_t *m =
        (const signed_manifest_t *)SIGNED_IMAGE_BASE;

    uart_init_115200_hsi16();

    uart_puts("\n========================================\n");
    uart_puts("STM32F429 SECURITY LAB\n");
    uart_puts("EXP045 BOOTLOADER V2\n");
    uart_puts("========================================\n");

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

    uart_puts("Computing SHA-512 and verifying Ed25519...\n");

    const verify_status_t status = boot_policy_verify();

    uart_puts("Verification     = ");
    uart_puts(signed_image_status_text(status));
    uart_puts("\n");

    if (status != VERIFY_OK) {
        uart_puts("Application will NOT be started.\n");
        uart_puts("Bootloader halted safely.\n");
        for (;;) {
        }
    }

    uart_puts("Signature and payload hash accepted.\n");
    uart_puts("Jumping to application...\n");
    delay_cycles(4000000U);

    signed_image_jump();

    for (;;) {
    }
}
