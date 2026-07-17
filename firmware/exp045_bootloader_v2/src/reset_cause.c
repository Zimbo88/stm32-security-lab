#include "reset_cause.h"

#include "uart.h"

#define REG32(address) (*(volatile uint32_t *)(address))

#define RCC_BASE        0x40023800UL
#define RCC_CSR         REG32(RCC_BASE + 0x74UL)

#define RCC_CSR_RMVF    (1UL << 24)
#define RCC_CSR_BORRSTF (1UL << 25)
#define RCC_CSR_PINRSTF (1UL << 26)
#define RCC_CSR_PORRSTF (1UL << 27)
#define RCC_CSR_SFTRSTF (1UL << 28)
#define RCC_CSR_IWDGRSTF (1UL << 29)
#define RCC_CSR_WWDGRSTF (1UL << 30)
#define RCC_CSR_LPWRRSTF (1UL << 31)

static uint8_t flag_is_set(uint32_t value, uint32_t mask)
{
    return (value & mask) != 0U ? 1U : 0U;
}

reset_cause_t reset_cause_capture(void)
{
    const uint32_t csr = RCC_CSR;

    reset_cause_t cause;

    cause.raw_csr = csr;
    cause.low_power_reset =
        flag_is_set(csr, RCC_CSR_LPWRRSTF);
    cause.window_watchdog_reset =
        flag_is_set(csr, RCC_CSR_WWDGRSTF);
    cause.independent_watchdog_reset =
        flag_is_set(csr, RCC_CSR_IWDGRSTF);
    cause.software_reset =
        flag_is_set(csr, RCC_CSR_SFTRSTF);
    cause.power_on_reset =
        flag_is_set(csr, RCC_CSR_PORRSTF);
    cause.pin_reset =
        flag_is_set(csr, RCC_CSR_PINRSTF);
    cause.brownout_reset =
        flag_is_set(csr, RCC_CSR_BORRSTF);

    return cause;
}

void reset_cause_clear(void)
{
    RCC_CSR |= RCC_CSR_RMVF;
}

static void print_flag(const char *name, uint8_t value)
{
    uart_puts(name);
    uart_puts(value != 0U ? "YES\n" : "NO\n");
}

void reset_cause_print(const reset_cause_t *cause)
{
    uart_puts("Reset cause raw RCC_CSR = ");
    uart_put_hex32(cause->raw_csr);
    uart_puts("\n");

    print_flag(
        "Low-power reset          = ",
        cause->low_power_reset
    );
    print_flag(
        "Window watchdog reset    = ",
        cause->window_watchdog_reset
    );
    print_flag(
        "Independent watchdog     = ",
        cause->independent_watchdog_reset
    );
    print_flag(
        "Software reset           = ",
        cause->software_reset
    );
    print_flag(
        "Power-on reset           = ",
        cause->power_on_reset
    );
    print_flag(
        "External pin reset       = ",
        cause->pin_reset
    );
    print_flag(
        "Brownout reset           = ",
        cause->brownout_reset
    );
}
