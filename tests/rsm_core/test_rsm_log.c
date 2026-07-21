#include <assert.h>
#include <stdint.h>

#include "log.h"

static uint32_t fake_millis;

uint32_t platform_millis(void)
{
    return fake_millis++;
}

void uart_putc(char value) { (void)value; }
void uart_puts(const char *text) { (void)text; }
void uart_put_hex32(uint32_t value) { (void)value; }
void uart_put_u32(uint32_t value) { (void)value; }

int main(void)
{
    uint8_t payload[13] = {0};

    log_init();
    assert(log_count() == 0UL);
    assert(log_dropped() == 0UL);
    assert(log_last_sequence() == 0UL);

    log_write(LOG_INFO, 1U, 2U, 0, 0U);
    assert(log_count() == 1UL);
    assert(log_dropped() == 0UL);
    assert(log_last_sequence() == 1UL);

    log_clear();
    assert(log_count() == 0UL);
    assert(log_dropped() == 0UL);
    assert(log_last_sequence() == 1UL);

    log_init();
    log_write(LOG_WARNING, 1U, 2U, payload, (uint8_t)sizeof(payload));
    assert(log_count() == 1UL);
    assert(log_dropped() == 1UL);
    assert(log_last_sequence() == 1UL);

    log_init();
    for (uint32_t i = 0U; i < 34U; ++i) {
        log_write(LOG_INFO, 1U, (uint16_t)i, 0, 0U);
    }
    assert(log_count() == 32UL);
    assert(log_dropped() == 2UL);
    assert(log_last_sequence() == 34UL);

    return 0;
}
