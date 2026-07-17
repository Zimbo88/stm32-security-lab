#ifndef PERFORMANCE_H
#define PERFORMANCE_H

#include <stdint.h>

void performance_init(uint32_t cpu_clock_hz);

uint32_t performance_cycles(void);
uint32_t performance_cycles_to_us(uint32_t cycles);

void performance_measurements_reset(void);

void performance_record_sha512_cycles(uint32_t cycles);
void performance_record_ed25519_cycles(uint32_t cycles);
void performance_record_verification_cycles(uint32_t cycles);

uint32_t performance_sha512_us(void);
uint32_t performance_ed25519_us(void);
uint32_t performance_verification_us(void);

#endif
