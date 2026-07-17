#include <stdint.h>

#include "boot_info.h"
#include "boot_policy.h"
#include "delay.h"
#include "signed_image.h"
#include "uart.h"

int main(void)
{
    uart_init_115200_hsi16();

    boot_info_print_banner();
    boot_info_print_manifest();

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
