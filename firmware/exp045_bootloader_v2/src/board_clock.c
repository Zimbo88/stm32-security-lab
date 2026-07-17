#include "board_clock.h"
#include "board.h"

void board_clock_init(void)
{
    /*
     * EXP063:
     * Intentionally keep the STM32 in its reset clock configuration.
     *
     * After reset:
     *   - HSI = 16 MHz
     *   - PLL disabled
     *   - HSE disabled
     *
     * Future experiments will configure the clock tree here.
     */
}

uint32_t board_clock_get_sysclk_hz(void)
{
    return BOARD_HSI_HZ;
}
