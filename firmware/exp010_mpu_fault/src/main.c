#include <stdint.h>

#include "clock.h"
#include "delay.h"
#include "gpio.h"
#include "mpu.h"
#include "uart.h"

int main(void)
{
    volatile uint32_t forbidden_value;

    heartbeat_init();

    if (clock_init_84mhz() == 0U) {
        uart_init(16000000UL, 115200UL);
        uart_puts("\nCLOCK INIT FAILED\n");

        for (;;) {
            heartbeat_toggle();
            delay_cycles(2000000U);
        }
    }

    uart_init(clock_get_apb2_hz(), 115200UL);

    uart_puts("\n");
    uart_puts("========================================\n");
    uart_puts("STM32F429 SECURITY LAB\n");
    uart_puts("EXP010 MPU AND FAULT CAPTURE\n");
    uart_puts("========================================\n");

    clock_report();

    uart_puts("\nProtected test region:\n");
    uart_puts("Base address     = ");
    uart_put_hex32(MPU_TEST_ADDRESS);
    uart_puts("\n");
    uart_puts("Region size      = 32 bytes\n");
    uart_puts("Permissions      = NO ACCESS, EXECUTE NEVER\n");

    mpu_configure_test_region();

    uart_puts("MPU enabled      = ");
    uart_puts(mpu_is_enabled() != 0U ? "YES\n" : "NO\n");

    uart_puts("\nThree heartbeats before deliberate access violation.\n");

    for (uint32_t i = 0U; i < 3U; ++i) {
        heartbeat_toggle();
        uart_puts("heartbeat ");
        uart_put_u32(i + 1U);
        uart_puts("\n");
        delay_cycles(21000000U);
    }

    uart_puts("\nAttempting forbidden read from ");
    uart_put_hex32(MPU_TEST_ADDRESS);
    uart_puts("\n");

    /*
     * Dieser Lesezugriff muss durch Region 7 blockiert werden.
     * Die Zuweisung wird normalerweise nicht mehr abgeschlossen.
     */
    forbidden_value =
        *(volatile const uint32_t *)MPU_TEST_ADDRESS;

    uart_puts("ERROR: protected read unexpectedly succeeded: ");
    uart_put_hex32(forbidden_value);
    uart_puts("\n");

    for (;;) {
        heartbeat_toggle();
        delay_cycles(21000000U);
    }
}
