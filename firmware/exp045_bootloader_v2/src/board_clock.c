#include "board.h"
#include "board_clock.h"

uint32_t board_sysclk_hz(void)
{
    return BOARD_HSI_HZ;
}

uint32_t board_hclk_hz(void)
{
    return board_sysclk_hz();
}

uint32_t board_pclk1_hz(void)
{
    return board_hclk_hz();
}

uint32_t board_pclk2_hz(void)
{
    return board_hclk_hz();
}
