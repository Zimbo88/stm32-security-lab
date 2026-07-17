#include "performance.h"

void performance_init(uint32_t cpu_clock_hz)
{
    (void)cpu_clock_hz;
}

uint32_t performance_cycles(void)
{
    return 0U;
}

uint32_t performance_cycles_to_us(uint32_t cycles)
{
    (void)cycles;
    return 0U;
}

void performance_measurements_reset(void)
{
}

void performance_record_sha512_cycles(uint32_t cycles)
{
    (void)cycles;
}

void performance_record_ed25519_cycles(uint32_t cycles)
{
    (void)cycles;
}

void performance_record_verification_cycles(uint32_t cycles)
{
    (void)cycles;
}

uint32_t performance_sha512_us(void)
{
    return 0U;
}

uint32_t performance_ed25519_us(void)
{
    return 0U;
}

uint32_t performance_verification_us(void)
{
    return 0U;
}
