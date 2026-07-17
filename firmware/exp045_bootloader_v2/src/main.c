#include "boot_sequence.h"
#include "uart.h"

int main(void)
{
    uart_init_115200_hsi16();
    boot_sequence_execute();
}
