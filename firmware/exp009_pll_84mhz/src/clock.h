#ifndef CLOCK_H
#define CLOCK_H

#include <stdint.h>

uint32_t clock_init_84mhz(void);
uint32_t clock_get_sysclk_hz(void);
uint32_t clock_get_apb2_hz(void);
void clock_report(void);

#endif
