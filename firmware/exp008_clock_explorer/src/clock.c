#include <stdint.h>

#include "clock.h"
#include "uart.h"

#define REG32(address) (*(volatile uint32_t *)(address))

#define RCC_BASE          0x40023800UL
#define RCC_CR            REG32(RCC_BASE + 0x00UL)
#define RCC_PLLCFGR       REG32(RCC_BASE + 0x04UL)
#define RCC_CFGR          REG32(RCC_BASE + 0x08UL)
#define RCC_AHB1ENR       REG32(RCC_BASE + 0x30UL)

#define FLASH_ACR         REG32(0x40023C00UL)

#define GPIOA_BASE        0x40020000UL
#define GPIOA_MODER       REG32(GPIOA_BASE + 0x00UL)
#define GPIOA_OTYPER      REG32(GPIOA_BASE + 0x04UL)
#define GPIOA_OSPEEDR     REG32(GPIOA_BASE + 0x08UL)
#define GPIOA_PUPDR       REG32(GPIOA_BASE + 0x0CUL)
#define GPIOA_AFRH        REG32(GPIOA_BASE + 0x24UL)

#define RCC_CR_HSION      (1UL << 0)
#define RCC_CR_HSIRDY     (1UL << 1)
#define RCC_CR_HSEON      (1UL << 16)
#define RCC_CR_HSERDY     (1UL << 17)
#define RCC_CR_PLLON      (1UL << 24)
#define RCC_CR_PLLRDY     (1UL << 25)

#define RCC_AHB1ENR_GPIOAEN (1UL << 0)

#define RCC_CFGR_SWS_MASK    (3UL << 2)
#define RCC_CFGR_MCO1_MASK   (3UL << 21)
#define RCC_CFGR_MCO1PRE_MASK (7UL << 24)

#define HSI_HZ 16000000UL
#define HSE_HZ 25000000UL

static uint32_t mco1_frequency_hz;

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

static void pa8_mco1_init(void)
{
    RCC_AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    (void)RCC_AHB1ENR;

    /* PA8: Alternate Function mode (10). */
    GPIOA_MODER &= ~(3UL << (8U * 2U));
    GPIOA_MODER |=  (2UL << (8U * 2U));

    /* Push-pull. */
    GPIOA_OTYPER &= ~(1UL << 8U);

    /* Very high output speed for clock output. */
    GPIOA_OSPEEDR &= ~(3UL << (8U * 2U));
    GPIOA_OSPEEDR |=  (3UL << (8U * 2U));

    /* No pull-up or pull-down. */
    GPIOA_PUPDR &= ~(3UL << (8U * 2U));

    /*
     * PA8 uses AF0 for MCO1.
     * AFRH bits 3:0 correspond to PA8.
     */
    GPIOA_AFRH &= ~(0xFUL << 0U);
}

void clock_probe_init(void)
{
    uint32_t timeout = 4000000UL;

    pa8_mco1_init();

    /*
     * Enable HSE without changing SYSCLK.
     * UART and CPU therefore continue using reset-default HSI.
     */
    RCC_CR |= RCC_CR_HSEON;

    while (((RCC_CR & RCC_CR_HSERDY) == 0U) && (timeout != 0U)) {
        --timeout;
    }

    RCC_CFGR &= ~(RCC_CFGR_MCO1_MASK |
                  RCC_CFGR_MCO1PRE_MASK);

    if ((RCC_CR & RCC_CR_HSERDY) != 0U) {
        /*
         * MCO1 source = HSE.
         * MCO1 prescaler = /5.
         * Expected PA8 output = 25 MHz / 5 = 5 MHz.
         */
        RCC_CFGR |= (2UL << 21U);
        RCC_CFGR |= (7UL << 24U);
        mco1_frequency_hz = HSE_HZ / 5UL;
    } else {
        /*
         * Fallback:
         * MCO1 source = HSI.
         * MCO1 prescaler = /4.
         * Expected PA8 output = 16 MHz / 4 = 4 MHz.
         */
        RCC_CFGR |= (6UL << 24U);
        mco1_frequency_hz = HSI_HZ / 4UL;
    }
}

uint32_t clock_hse_is_ready(void)
{
    return ((RCC_CR & RCC_CR_HSERDY) != 0U) ? 1UL : 0UL;
}

uint32_t clock_get_mco1_hz(void)
{
    return mco1_frequency_hz;
}

uint32_t clock_get_sysclk_hz(void)
{
    const uint32_t sws = RCC_CFGR & RCC_CFGR_SWS_MASK;

    if (sws == (0UL << 2U)) {
        return HSI_HZ;
    }

    if (sws == (1UL << 2U)) {
        return HSE_HZ;
    }

    if (sws == (2UL << 2U)) {
        const uint32_t pllcfgr = RCC_PLLCFGR;
        const uint32_t pllm = pllcfgr & 0x3FUL;
        const uint32_t plln = (pllcfgr >> 6U) & 0x1FFUL;
        const uint32_t pllp_bits = (pllcfgr >> 16U) & 0x3UL;
        const uint32_t pllp = (pllp_bits + 1UL) * 2UL;
        const uint32_t pll_source =
            ((pllcfgr & (1UL << 22U)) != 0U) ? HSE_HZ : HSI_HZ;

        if ((pllm == 0U) || (pllp == 0U)) {
            return 0U;
        }

        return ((pll_source / pllm) * plln) / pllp;
    }

    return 0U;
}

void clock_report(void)
{
    uart_puts("\nClock registers:\n");

    print_hex("RCC_CR          ", RCC_CR);
    print_hex("RCC_PLLCFGR     ", RCC_PLLCFGR);
    print_hex("RCC_CFGR        ", RCC_CFGR);
    print_hex("FLASH_ACR       ", FLASH_ACR);

    uart_puts("\nClock status:\n");

    uart_puts("HSI ready        = ");
    uart_puts(((RCC_CR & RCC_CR_HSIRDY) != 0U) ? "YES\n" : "NO\n");

    uart_puts("HSE ready        = ");
    uart_puts(clock_hse_is_ready() ? "YES\n" : "NO\n");

    uart_puts("PLL enabled      = ");
    uart_puts(((RCC_CR & RCC_CR_PLLON) != 0U) ? "YES\n" : "NO\n");

    uart_puts("PLL ready        = ");
    uart_puts(((RCC_CR & RCC_CR_PLLRDY) != 0U) ? "YES\n" : "NO\n");

    print_u32("SYSCLK Hz       ", clock_get_sysclk_hz());
    print_u32("MCO1 output Hz  ", clock_get_mco1_hz());

    if (clock_hse_is_ready() != 0U) {
        uart_puts("MCO1 source      = HSE / 5\n");
    } else {
        uart_puts("MCO1 source      = HSI / 4 fallback\n");
    }

    uart_puts("MCO1 pin         = PA8 / AF0\n");
}
