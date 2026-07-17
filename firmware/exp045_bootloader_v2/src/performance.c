#include "performance.h"

#define DEMCR           (*(volatile uint32_t *)0xE000EDFCUL)
#define DWT_CTRL        (*(volatile uint32_t *)0xE0001000UL)
#define DWT_CYCCNT      (*(volatile uint32_t *)0xE0001004UL)

#define DEMCR_TRCENA    (1UL << 24)
#define DWT_CYCCNTENA   (1UL << 0)

static uint32_t performance_cpu_clock_hz;
static uint32_t sha512_cycles;
static uint32_t ed25519_cycles;
static uint32_t verification_cycles;

void performance_init(uint32_t cpu_clock_hz)
{
    performance_cpu_clock_hz = cpu_clock_hz;

    DEMCR |= DEMCR_TRCENA;
    DWT_CYCCNT = 0U;
    DWT_CTRL |= DWT_CYCCNTENA;

    performance_measurements_reset();
}

uint32_t performance_cycles(void)
{
    return DWT_CYCCNT;
}

uint32_t performance_cycles_to_us(uint32_t cycles)
{
    if (performance_cpu_clock_hz < 1000000UL) {
        return 0U;
    }

    return cycles / (performance_cpu_clock_hz / 1000000UL);
}

void performance_measurements_reset(void)
{
    sha512_cycles = 0U;
    ed25519_cycles = 0U;
    verification_cycles = 0U;
}

void performance_record_sha512_cycles(uint32_t cycles)
{
    sha512_cycles = cycles;
}

void performance_record_ed25519_cycles(uint32_t cycles)
{
    ed25519_cycles = cycles;
}

void performance_record_verification_cycles(uint32_t cycles)
{
    verification_cycles = cycles;
}

uint32_t performance_sha512_us(void)
{
    return performance_cycles_to_us(sha512_cycles);
}

uint32_t performance_ed25519_us(void)
{
    return performance_cycles_to_us(ed25519_cycles);
}

uint32_t performance_verification_us(void)
{
    return performance_cycles_to_us(verification_cycles);
}
