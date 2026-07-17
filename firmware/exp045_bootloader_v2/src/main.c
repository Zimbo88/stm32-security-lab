#include <stdint.h>

#include "boot_info.h"
#include "boot_policy.h"
#include "boot_result.h"
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

    boot_result_print(status);

    if (status != VERIFY_OK) {
        boot_result_halt();
    }

    uart_puts("Signature and payload hash accepted.\n");
    uart_puts("Jumping to application...\n");
    delay_cycles(4000000U);

    signed_image_jump();

    for (;;) {
    }
}
