#include "boot_sequence.h"
#include "board_clock.h"
#include "update_mode.h"
#include "uart.h"

int main(void)
{
    update_service_result_t update_result;

    board_clock_init();
    uart_init();
    update_result = update_mode_poll_and_process();
    if (update_result == UPDATE_SERVICE_RESULT_RESET_REQUESTED) {
        for (;;) {
        }
    }
    if (update_result == UPDATE_SERVICE_RESULT_RECOVERY_REQUESTED) {
        update_mode_run_recovery();
    }
    boot_sequence_execute();
}
