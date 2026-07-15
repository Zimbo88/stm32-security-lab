#include <stdint.h>

#include "delay.h"
#include "gpio.h"
#include "registers.h"
#include "uart.h"

static uint32_t read_msp(void)
{
    uint32_t value;

    __asm volatile (
        "mrs %0, msp"
        : "=r" (value)
    );

    return value;
}

static void print_hex(const char *name, uint32_t value)
{
    uart_puts(name);
    uart_puts(" = ");
    uart_put_hex32(value);
    uart_puts("\n");
}

static void print_reset_flags(uint32_t csr)
{
    uart_puts("\nReset flags:\n");

    if ((csr & RCC_CSR_BORRSTF) != 0U) {
        uart_puts("  BORRSTF  : Brownout reset detected\n");
    }

    if ((csr & RCC_CSR_PINRSTF) != 0U) {
        uart_puts("  PINRSTF  : NRST/pin reset detected\n");
    }

    if ((csr & RCC_CSR_PORRSTF) != 0U) {
        uart_puts("  PORRSTF  : Power-on reset detected\n");
    }

    if ((csr & RCC_CSR_SFTRSTF) != 0U) {
        uart_puts("  SFTRSTF  : Software reset detected\n");
    }

    if ((csr & RCC_CSR_IWDGRSTF) != 0U) {
        uart_puts("  IWDGRSTF : Independent watchdog reset\n");
    }

    if ((csr & RCC_CSR_WWDGRSTF) != 0U) {
        uart_puts("  WWDGRSTF : Window watchdog reset\n");
    }

    if ((csr & RCC_CSR_LPWRRSTF) != 0U) {
        uart_puts("  LPWRRSTF : Low-power reset detected\n");
    }

    if ((csr & 0xFE000000UL) == 0U) {
        uart_puts("  No reset source flag is currently set\n");
    }
}

int main(void)
{
    /*
     * Reset flags must be captured before they are cleared.
     * They may remain set across resets until RMVF is written.
     */
    const uint32_t reset_flags = RCC_CSR;

    heartbeat_init();
    uart_init();

    const uint32_t idcode = DBGMCU_IDCODE;
    const uint32_t device_id = idcode & 0x0FFFUL;
    const uint32_t revision_id = idcode >> 16U;
    const uint32_t msp = read_msp();

    uart_puts("\n");
    uart_puts("========================================\n");
    uart_puts("STM32F429 SECURITY LAB\n");
    uart_puts("EXP005 SYSTEM IDENTITY\n");
    uart_puts("========================================\n");

    print_hex("DBGMCU_IDCODE ", idcode);
    print_hex("DEVICE_ID     ", device_id);
    print_hex("REVISION_ID   ", revision_id);

    uart_puts("FLASH_SIZE_KB  = ");
    uart_put_u32((uint32_t)FLASH_SIZE_KB);
    uart_puts("\n");

    print_hex("UID_WORD0     ", UID_WORD0);
    print_hex("UID_WORD1     ", UID_WORD1);
    print_hex("UID_WORD2     ", UID_WORD2);

    print_hex("SCB_VTOR      ", SCB_VTOR);
    print_hex("MSP           ", msp);
    print_hex("RCC_CSR       ", reset_flags);

    print_reset_flags(reset_flags);

    uart_puts("\nReset flags will now be cleared using RMVF.\n");
    RCC_CSR |= RCC_CSR_RMVF;

    uart_puts("EXP005 READY\n");

    for (;;) {
        heartbeat_toggle();
        uart_puts("heartbeat\n");
        delay_cycles(8000000U);
    }
}
