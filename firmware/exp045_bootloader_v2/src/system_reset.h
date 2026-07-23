#ifndef SYSTEM_RESET_H
#define SYSTEM_RESET_H

#include <stdint.h>

void system_reset_request(void);

#ifdef SYSTEM_RESET_HOST_TEST
void system_reset_host_reset(void);
uint32_t system_reset_host_requested(void);
uint32_t system_reset_host_aircr(void);
#endif

#endif
