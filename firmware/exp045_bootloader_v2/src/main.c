#include "boot_sequence.h"
#include "board_clock.h"
#include "uart.h"

int main(void)
{
    board_clock_init();
    uart_init();
    boot_sequence_execute();
}
