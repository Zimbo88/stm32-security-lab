#include "boot_sequence.h"

#include "boot_info.h"
#include "boot_mode.h"
#include "boot_policy.h"
#include "boot_result.h"
#include "delay.h"
#include "recovery_policy.h"
#include "signed_image.h"
#include "uart.h"

void boot_sequence_execute(void)
{
    boot_info_print_banner();

    const boot_mode_t mode = boot_mode_detect();
    const recovery_policy_status_t recovery_status =
        recovery_policy_evaluate(mode);

    uart_puts("Boot mode        = ");
    uart_puts(boot_mode_text(mode));
    uart_puts("\n");

    uart_puts("Recovery policy  = ");
    uart_puts(recovery_policy_status_text(recovery_status));
    uart_puts("\n");

    if (recovery_status == RECOVERY_POLICY_UNAVAILABLE) {
        uart_puts("Recovery request rejected safely.\n");
        boot_result_halt();
    }

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

    boot_result_halt();
}
