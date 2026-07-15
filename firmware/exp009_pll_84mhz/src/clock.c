#include <stdint.h>

#include "clock.h"
#include "uart.h"

#define REG32(a) (*(volatile uint32_t *)(a))

#define RCC_BASE        0x40023800UL
#define RCC_CR          REG32(RCC_BASE + 0x00UL)
#define RCC_PLLCFGR     REG32(RCC_BASE + 0x04UL)
#define RCC_CFGR        REG32(RCC_BASE + 0x08UL)
#define RCC_APB1ENR     REG32(RCC_BASE + 0x40UL)

#define FLASH_ACR       REG32(0x40023C00UL)

#define PWR_BASE        0x40007000UL
#define PWR_CR          REG32(PWR_BASE + 0x00UL)

#define RCC_CR_HSEON    (1UL << 16)
#define RCC_CR_HSERDY   (1UL << 17)
#define RCC_CR_PLLON    (1UL << 24)
#define RCC_CR_PLLRDY   (1UL << 25)

#define RCC_APB1ENR_PWREN (1UL << 28)

#define RCC_CFGR_SW_MASK    (3UL << 0)
#define RCC_CFGR_SWS_MASK   (3UL << 2)
#define RCC_CFGR_HPRE_MASK  (0xFUL << 4)
#define RCC_CFGR_PPRE1_MASK (7UL << 10)
#define RCC_CFGR_PPRE2_MASK (7UL << 13)

#define FLASH_ACR_LATENCY_2WS 2UL
#define FLASH_ACR_PRFTEN      (1UL << 8)
#define FLASH_ACR_ICEN        (1UL << 9)
#define FLASH_ACR_DCEN        (1UL << 10)

static uint32_t sysclk_hz = 16000000UL;
static uint32_t apb2_hz = 16000000UL;

static void print_hex(const char *name, uint32_t value)
{
    uart_puts(name);
    uart_puts(" = ");
    uart_put_hex32(value);
    uart_puts("\n");
}

static void print_u32(const char *name, uint32_t value)
{
    uart_puts(name);
    uart_puts(" = ");
    uart_put_u32(value);
    uart_puts("\n");
}

uint32_t clock_init_84mhz(void)
{
    uint32_t timeout;

    RCC_APB1ENR |= RCC_APB1ENR_PWREN;
    (void)RCC_APB1ENR;

    /* Voltage scale 1. */
    PWR_CR &= ~(3UL << 14);
    PWR_CR |=  (3UL << 14);

    RCC_CR |= RCC_CR_HSEON;

    timeout = 4000000UL;
    while (((RCC_CR & RCC_CR_HSERDY) == 0U) && (timeout != 0U)) {
        --timeout;
    }

    if ((RCC_CR & RCC_CR_HSERDY) == 0U) {
        return 0U;
    }

    /* Flash: 2 wait states, caches and prefetch enabled. */
    FLASH_ACR =
        FLASH_ACR_LATENCY_2WS |
        FLASH_ACR_PRFTEN |
        FLASH_ACR_ICEN |
        FLASH_ACR_DCEN;

    /*
     * PLL source: HSE 25 MHz
     * PLLM = 25
     * PLLN = 336
     * PLLP = 4
     * PLLQ = 7
     *
     * VCO input  = 25 / 25 = 1 MHz
     * VCO output = 1 * 336 = 336 MHz
     * SYSCLK     = 336 / 4 = 84 MHz
     * PLL48      = 336 / 7 = 48 MHz
     */
    RCC_CR &= ~RCC_CR_PLLON;
    while ((RCC_CR & RCC_CR_PLLRDY) != 0U) {
    }

    RCC_PLLCFGR =
        (25UL << 0) |
        (336UL << 6) |
        (1UL << 16) |   /* PLLP = 4 */
        (1UL << 22) |   /* HSE source */
        (7UL << 24);

    /*
     * AHB  = SYSCLK / 1 = 84 MHz
     * APB1 = HCLK / 2   = 42 MHz
     * APB2 = HCLK / 1   = 84 MHz
     */
    RCC_CFGR &=
        ~(RCC_CFGR_HPRE_MASK |
          RCC_CFGR_PPRE1_MASK |
          RCC_CFGR_PPRE2_MASK);

    RCC_CFGR |= (4UL << 10); /* APB1 /2 */

    RCC_CR |= RCC_CR_PLLON;

    timeout = 4000000UL;
    while (((RCC_CR & RCC_CR_PLLRDY) == 0U) && (timeout != 0U)) {
        --timeout;
    }

    if ((RCC_CR & RCC_CR_PLLRDY) == 0U) {
        return 0U;
    }

    RCC_CFGR &= ~RCC_CFGR_SW_MASK;
    RCC_CFGR |=  (2UL << 0); /* PLL as SYSCLK */

    timeout = 4000000UL;
    while (((RCC_CFGR & RCC_CFGR_SWS_MASK) != (2UL << 2)) &&
           (timeout != 0U)) {
        --timeout;
    }

    if ((RCC_CFGR & RCC_CFGR_SWS_MASK) != (2UL << 2)) {
        return 0U;
    }

    sysclk_hz = 84000000UL;
    apb2_hz = 84000000UL;

    return 1U;
}

uint32_t clock_get_sysclk_hz(void)
{
    return sysclk_hz;
}

uint32_t clock_get_apb2_hz(void)
{
    return apb2_hz;
}

void clock_report(void)
{
    uart_puts("\nClock registers:\n");
    print_hex("RCC_CR          ", RCC_CR);
    print_hex("RCC_PLLCFGR     ", RCC_PLLCFGR);
    print_hex("RCC_CFGR        ", RCC_CFGR);
    print_hex("FLASH_ACR       ", FLASH_ACR);
    print_hex("PWR_CR          ", PWR_CR);

    uart_puts("\nClock status:\n");
    print_u32("SYSCLK Hz       ", sysclk_hz);
    print_u32("APB2 Hz         ", apb2_hz);
    uart_puts("Clock source     = PLL from HSE\n");
}
