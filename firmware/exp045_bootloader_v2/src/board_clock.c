#include "board_clock.h"
#include "board.h"

#define REG32(a) (*(volatile uint32_t *)(a))

/*---------------------------------------------------------------------------
 * Peripheral base addresses
 *---------------------------------------------------------------------------*/

#define RCC_BASE        0x40023800UL
#define PWR_BASE        0x40007000UL
#define FLASH_BASE      0x40023C00UL

/*---------------------------------------------------------------------------
 * RCC registers
 *---------------------------------------------------------------------------*/

#define RCC_CR          REG32(RCC_BASE + 0x00UL)
#define RCC_PLLCFGR     REG32(RCC_BASE + 0x04UL)
#define RCC_CFGR        REG32(RCC_BASE + 0x08UL)

/*---------------------------------------------------------------------------
 * PWR registers
 *---------------------------------------------------------------------------*/

#define PWR_CR          REG32(PWR_BASE + 0x00UL)
#define PWR_CSR         REG32(PWR_BASE + 0x04UL)

/*---------------------------------------------------------------------------
 * FLASH registers
 *---------------------------------------------------------------------------*/

#define FLASH_ACR       REG32(FLASH_BASE + 0x00UL)

/*---------------------------------------------------------------------------
 * Frequently used RCC bits
 *---------------------------------------------------------------------------*/

#define RCC_CR_HSION        (1UL << 0)
#define RCC_CR_HSIRDY       (1UL << 1)
#define RCC_CR_HSEON        (1UL << 16)
#define RCC_CR_HSERDY       (1UL << 17)
#define RCC_CR_PLLON        (1UL << 24)
#define RCC_CR_PLLRDY       (1UL << 25)


/*---------------------------------------------------------------------------
 * FLASH bits
 *---------------------------------------------------------------------------*/

#define FLASH_ACR_LATENCY_5WS    (5UL << 0)
#define FLASH_ACR_ICEN           (1UL << 9)
#define FLASH_ACR_DCEN           (1UL << 10)
#define FLASH_ACR_PRFTEN         (1UL << 8)

/*---------------------------------------------------------------------------
 * PWR bits
 *---------------------------------------------------------------------------*/

#define PWR_CR_VOS_SCALE1        (3UL << 14)
#define PWR_CR_ODEN              (1UL << 16)
#define PWR_CR_ODSWEN            (1UL << 17)

#define PWR_CSR_ODRDY            (1UL << 16)
#define PWR_CSR_ODSWRDY          (1UL << 17)

void board_clock_init(void)
{
    /*
     * EXP064:
     * Keep the MCU in its reset clock configuration.
     *
     * This file now owns the clock-related register definitions.
     * Future experiments will configure HSE, PLL, FLASH wait states
     * and OverDrive here.
     */
}

uint32_t board_clock_get_sysclk_hz(void)
{
    return BOARD_HSI_HZ;
}
