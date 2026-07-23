#include "boot_sequence.h"
#include "board_clock.h"
#include "update_mode.h"
#include "uart.h"

int main(void)
{
    board_clock_init();
    uart_init();
    if (update_mode_poll_and_process() ==
        UPDATE_SERVICE_RESULT_RESET_REQUESTED) {
        for (;;) {
        }
    }
    boot_sequence_execute();
}
