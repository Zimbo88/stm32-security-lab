#ifndef BOARD_CLOCK_H
#define BOARD_CLOCK_H

#include <stdint.h>

uint32_t board_sysclk_hz(void);
uint32_t board_hclk_hz(void);
uint32_t board_pclk1_hz(void);
uint32_t board_pclk2_hz(void);

#endif
