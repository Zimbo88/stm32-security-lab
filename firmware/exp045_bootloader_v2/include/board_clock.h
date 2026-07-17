#ifndef BOARD_CLOCK_H
#define BOARD_CLOCK_H

#include <stdint.h>

void board_clock_init(void);

uint32_t board_clock_get_sysclk_hz(void);

#endif
