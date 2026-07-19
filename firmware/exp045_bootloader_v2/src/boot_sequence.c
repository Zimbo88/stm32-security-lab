#include "boot_sequence.h"

#include "board_clock.h"
#include "boot_info.h"
#include "boot_mode.h"
#include "boot_policy.h"
#include "boot_result.h"
#include "delay.h"
#include "performance.h"
#include "recovery_policy.h"
#include "reset_cause.h"
#include "signed_image.h"
#include "uart.h"

static void boot_performance_print(void)
{
    uart_puts("\nBOOT PERFORMANCE\n");

    uart_puts("CPU clock        = ");
    uart_put_u32(board_clock_get_sysclk_hz());
    uart_puts(" Hz\n");

    uart_puts("SHA-512          = ");
    uart_put_u32(performance_sha512_us());
    uart_puts(" us\n");

    uart_puts("Ed25519          = ");
    uart_put_u32(performance_ed25519_us());
    uart_puts(" us\n");

    uart_puts("Verification     = ");
    uart_put_u32(performance_verification_us());
    uart_puts(" us\n\n");
}

_Noreturn void boot_sequence_execute(void)
{
    performance_init(board_clock_get_sysclk_hz());

    const reset_cause_t reset_cause = reset_cause_capture();
    boot_info_print_banner();
    reset_cause_print(&reset_cause);
    reset_cause_clear();

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

    uart_puts("Selecting boot slot and verifying image...\n");

    performance_measurements_reset();

    const uint32_t verification_start = performance_cycles();
    boot_slot_selection_result_t selection;
    const boot_slot_selection_status_t selection_status =
        boot_policy_select(&selection);
    const uint32_t verification_end = performance_cycles();

    performance_record_verification_cycles(
        verification_end - verification_start
    );

    uart_puts("Slot policy      = ");
    uart_puts(boot_slot_selection_status_text(selection_status));
    uart_puts("\n");

    uart_puts("Slot decision    = ");
    uart_puts(boot_slot_selection_decision_text(selection.decision));
    uart_puts("\n");

    boot_result_print(selection.selected_verify_status);
    boot_performance_print();

    if (selection_status != BOOT_SLOT_SELECTION_OK) {
        boot_result_halt();
    }

    uart_puts("Signature and payload hash accepted.\n");
    uart_puts("Jumping to application...\n");
    delay_cycles(4000000U);

    const verify_status_t jump_status = signed_image_jump(&selection.jump_context);
    boot_result_print(jump_status);

    boot_result_halt();
}
