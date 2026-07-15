#ifndef CLOCK_H
#define CLOCK_H

#include <stdint.h>

void clock_probe_init(void);
void clock_report(void);

uint32_t clock_get_sysclk_hz(void);
uint32_t clock_get_mco1_hz(void);
uint32_t clock_hse_is_ready(void);

#endif
