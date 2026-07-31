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

#define RESET_KNOWN_FLAGS (RCC_CSR_BORRSTF | RCC_CSR_PINRSTF | \
                           RCC_CSR_PORRSTF | RCC_CSR_SFTRSTF | \
                           RCC_CSR_IWDGRSTF | RCC_CSR_WWDGRSTF | \
                           RCC_CSR_LPWRRSTF)

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

reset_cause_kind_t reset_cause_primary_kind_from_raw(uint32_t raw_csr)
{
    /* Watchdog first: a combined flag must preserve the most actionable cause. */
    if ((raw_csr & RCC_CSR_IWDGRSTF) != 0UL) {
        return RESET_CAUSE_KIND_IWDG;
    }
    if ((raw_csr & RCC_CSR_WWDGRSTF) != 0UL) {
        return RESET_CAUSE_KIND_WWDG;
    }
    if ((raw_csr & RCC_CSR_SFTRSTF) != 0UL) {
        return RESET_CAUSE_KIND_SOFTWARE;
    }
    if ((raw_csr & RCC_CSR_BORRSTF) != 0UL) {
        return RESET_CAUSE_KIND_BROWNOUT;
    }
    if ((raw_csr & RCC_CSR_PINRSTF) != 0UL) {
        return RESET_CAUSE_KIND_PIN;
    }
    if ((raw_csr & RCC_CSR_PORRSTF) != 0UL) {
        return RESET_CAUSE_KIND_POWER_ON;
    }
    if ((raw_csr & RCC_CSR_LPWRRSTF) != 0UL) {
        return RESET_CAUSE_KIND_LOW_POWER;
    }
    return ((raw_csr & RESET_KNOWN_FLAGS) == 0UL)
        ? RESET_CAUSE_KIND_NONE
        : RESET_CAUSE_KIND_UNKNOWN;
}

reset_cause_kind_t reset_cause_primary_kind(const reset_cause_t *cause)
{
    return (cause == NULL)
        ? RESET_CAUSE_KIND_UNKNOWN
        : reset_cause_primary_kind_from_raw(cause->raw_csr);
}

const char *reset_cause_kind_text(reset_cause_kind_t kind)
{
    switch (kind) {
    case RESET_CAUSE_KIND_NONE:       return "NONE";
    case RESET_CAUSE_KIND_IWDG:       return "IWDG";
    case RESET_CAUSE_KIND_WWDG:       return "WWDG";
    case RESET_CAUSE_KIND_SOFTWARE:   return "SOFTWARE";
    case RESET_CAUSE_KIND_BROWNOUT:   return "BROWNOUT";
    case RESET_CAUSE_KIND_PIN:        return "PIN";
    case RESET_CAUSE_KIND_POWER_ON:   return "POWER_ON";
    case RESET_CAUSE_KIND_LOW_POWER:  return "LOW_POWER";
    case RESET_CAUSE_KIND_UNKNOWN:
    default:                          return "UNKNOWN";
    }
}

uint32_t reset_cause_trial_result(uint32_t raw_csr)
{
    switch (reset_cause_primary_kind_from_raw(raw_csr)) {
    case RESET_CAUSE_KIND_IWDG:      return RESET_CAUSE_RESULT_IWDG;
    case RESET_CAUSE_KIND_WWDG:      return RESET_CAUSE_RESULT_WWDG;
    case RESET_CAUSE_KIND_SOFTWARE:  return RESET_CAUSE_RESULT_SOFTWARE;
    case RESET_CAUSE_KIND_BROWNOUT:  return RESET_CAUSE_RESULT_BROWNOUT;
    case RESET_CAUSE_KIND_PIN:       return RESET_CAUSE_RESULT_PIN;
    case RESET_CAUSE_KIND_POWER_ON:  return RESET_CAUSE_RESULT_POWER_ON;
    case RESET_CAUSE_KIND_LOW_POWER: return RESET_CAUSE_RESULT_LOW_POWER;
    case RESET_CAUSE_KIND_NONE:      return RESET_CAUSE_RESULT_NONE;
    case RESET_CAUSE_KIND_UNKNOWN:
    default:                         return RESET_CAUSE_RESULT_UNKNOWN;
    }
}

static void print_flag(const char *name, uint8_t value)
{
    uart_puts(name);
    uart_puts(value != 0U ? "YES\n" : "NO\n");
}

void reset_cause_print(const reset_cause_t *cause)
{
    if (cause == NULL) {
        return;
    }

    uart_puts("BOOT_RESET cause=");
    uart_puts(reset_cause_kind_text(reset_cause_primary_kind(cause)));
    uart_puts(" raw=");
    uart_put_hex32(cause->raw_csr);
    uart_puts("\n");
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
