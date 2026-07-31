#include "boot_sequence.h"

#include "board_clock.h"
#include "boot_info.h"
#include "boot_mode.h"
#include "boot_policy.h"
#include "boot_result.h"
#include "boot_watchdog.h"
#include "delay.h"
#include "led_show.h"
#include "performance.h"
#include "recovery_policy.h"
#include "reset_cause.h"
#include "signed_image.h"
#include "update_mode.h"
#include "uart.h"

#define BOOT_LED_BOOT_DELAY_CYCLES 700000U

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
    boot_watchdog_refresh();
    performance_init(board_clock_get_sysclk_hz());
    led_show_init();
    led_show_indicate(BOOT_LED_STATE_BOOTING);
    delay_cycles(BOOT_LED_BOOT_DELAY_CYCLES);
    led_show_all_off();

    const reset_cause_t reset_cause = reset_cause_capture();
    boot_info_print_banner();
    reset_cause_print(&reset_cause);

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
    led_show_indicate(BOOT_LED_STATE_VERIFYING);

    performance_measurements_reset();

    const uint32_t verification_start = performance_cycles();
    boot_slot_selection_result_t selection;
    const boot_slot_selection_status_t selection_status =
        boot_policy_select(reset_cause.raw_csr, &selection);
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

    if (selection_status == BOOT_SLOT_SELECTION_OK) {
        boot_result_print(selection.selected_verify_status);
    } else {
        uart_puts("Selected verify  = ");
        uart_puts(signed_image_status_text(selection.selected_verify_status));
        uart_puts("\n");

        uart_puts("Confirmed verify = ");
        uart_puts(signed_image_status_text(selection.confirmed_verify_status));
        uart_puts("\n");

        uart_puts("Candidate verify = ");
        uart_puts(signed_image_status_text(selection.candidate_verify_status));
        uart_puts("\n");

        uart_puts("Fallback cause   = ");
        uart_puts(boot_slot_selection_status_text(selection.fallback_cause));
        uart_puts("\n");

        boot_result_print(selection.selected_verify_status);
    }
    boot_performance_print();

    if (selection_status != BOOT_SLOT_SELECTION_OK) {
        uart_puts("RECOVERY ENTER reason=boot-policy-failure\n");
        reset_cause_clear();
        update_mode_run_recovery();
    }

    uart_puts("Signature and payload hash accepted.\n");
    uart_puts("Jumping to application...\n");
    led_show_all_off();
    delay_cycles(4000000U);

    const verify_status_t jump_status = signed_image_jump(&selection.jump_context);
    boot_result_print(jump_status);

    led_show_halt(BOOT_LED_STATE_FATAL);
}
