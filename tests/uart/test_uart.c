#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "byte_reader.h"
#include "uart.h"

static int failures;

static void expect_u32(const char *name, uint32_t expected, uint32_t actual)
{
    if (expected != actual) {
        printf(
            "%s: expected 0x%08X, got 0x%08X\n",
            name,
            (unsigned int)expected,
            (unsigned int)actual
        );
        ++failures;
    }
}

static void expect_size(const char *name, size_t expected, size_t actual)
{
    if (expected != actual) {
        printf(
            "%s: expected %lu, got %lu\n",
            name,
            (unsigned long)expected,
            (unsigned long)actual
        );
        ++failures;
    }
}

static void expect_status(
    const char *name,
    uart_status_t expected,
    uart_status_t actual
)
{
    if (expected != actual) {
        printf(
            "%s: expected %s, got %s\n",
            name,
            uart_status_text(expected),
            uart_status_text(actual)
        );
        ++failures;
    }
}

static void expect_reader_status(
    const char *name,
    byte_reader_status_t expected,
    byte_reader_status_t actual
)
{
    if (expected != actual) {
        printf(
            "%s: expected %s, got %s\n",
            name,
            byte_reader_status_text(expected),
            byte_reader_status_text(actual)
        );
        ++failures;
    }
}

typedef struct {
    const uint8_t *data;
    size_t length;
    size_t offset;
    uint32_t no_data_before_next_byte;
    uint32_t calls;
    byte_reader_status_t terminal_status;
} fake_reader_t;

static byte_reader_status_t fake_getc(void *context, uint8_t *byte)
{
    fake_reader_t *fake = (fake_reader_t *)context;

    if ((fake == NULL) || (byte == NULL)) {
        return BYTE_READER_ERR_INVALID_ARGUMENT;
    }

    ++fake->calls;
    if (fake->no_data_before_next_byte != 0U) {
        --fake->no_data_before_next_byte;
        return BYTE_READER_NO_DATA;
    }

    if (fake->offset < fake->length) {
        *byte = fake->data[fake->offset++];
        return BYTE_READER_OK;
    }

    return fake->terminal_status;
}

static void test_byte_reader_timeout_and_partial_read(void)
{
    static const uint8_t payload[] = {'A', 'B'};
    fake_reader_t fake;
    byte_reader_t reader;
    uint8_t byte = 0U;
    uint8_t buffer[3] = {0U, 0U, 0U};
    size_t bytes_read = 99U;

    memset(&fake, 0, sizeof(fake));
    fake.data = payload;
    fake.length = sizeof(payload);
    fake.no_data_before_next_byte = 2U;
    fake.terminal_status = BYTE_READER_NO_DATA;
    byte_reader_init(&reader, fake_getc, &fake);

    expect_reader_status(
        "byte reader delayed getc",
        BYTE_READER_OK,
        byte_reader_getc_timeout(&reader, &byte, 3U)
    );
    expect_u32("byte reader delayed byte", 'A', byte);
    expect_u32("byte reader delayed calls", 3U, fake.calls);

    expect_reader_status(
        "byte reader immediate timeout",
        BYTE_READER_TIMEOUT,
        byte_reader_getc_timeout(&reader, &byte, 0U)
    );
    expect_reader_status(
        "byte reader invalid before timeout",
        BYTE_READER_ERR_INVALID_ARGUMENT,
        byte_reader_getc_timeout(&reader, NULL, 0U)
    );

    fake.no_data_before_next_byte = 0U;
    fake.calls = 0U;
    expect_reader_status(
        "byte reader partial read",
        BYTE_READER_TIMEOUT,
        byte_reader_read_timeout(&reader, buffer, sizeof(buffer), 1U, &bytes_read)
    );
    expect_size("byte reader partial count", 1U, bytes_read);
    expect_u32("byte reader partial byte", 'B', buffer[0]);

    expect_reader_status(
        "byte reader null reader",
        BYTE_READER_ERR_INVALID_ARGUMENT,
        byte_reader_getc_nonblocking(NULL, &byte)
    );
}

static void set_rx(uint32_t flags, uint8_t byte)
{
    uart_host_set_usart_dr(byte);
    uart_host_set_usart_sr(UART_HOST_USART_SR_TXE | flags);
}

static void expect_rx_cleared(const char *name)
{
    expect_u32(
        name,
        UART_HOST_USART_SR_TXE,
        uart_host_get_usart_sr()
    );
}

static void test_uart_init_and_tx(void)
{
    uart_host_reset_registers();
    uart_init();

    expect_u32("GPIOA clock enabled", 1U, uart_host_get_rcc_ahb1enr() & 1U);
    expect_u32("USART1 clock enabled", 1U << 4, uart_host_get_rcc_apb2enr() & (1U << 4));
    expect_u32("USART1 BRR 16 MHz 115200", 0x008B, uart_host_get_usart_brr());
    expect_u32(
        "USART1 CR1 UE TE RE",
        UART_HOST_USART_CR1_UE | UART_HOST_USART_CR1_TE | UART_HOST_USART_CR1_RE,
        uart_host_get_usart_cr1()
    );
    expect_u32("USART1 CR2 reset", 0U, uart_host_get_usart_cr2());
    expect_u32("USART1 CR3 reset", 0U, uart_host_get_usart_cr3());

    expect_u32(
        "PA9/PA10 alternate mode",
        (2U << 18) | (2U << 20),
        uart_host_get_gpioa_moder() & ((3U << 18) | (3U << 20))
    );
    expect_u32(
        "PA9/PA10 push-pull",
        0U,
        uart_host_get_gpioa_otyper() & ((1U << 9) | (1U << 10))
    );
    expect_u32(
        "PA10 pull-up",
        1U << 20,
        uart_host_get_gpioa_pupdr() & ((3U << 18) | (3U << 20))
    );
    expect_u32(
        "PA9/PA10 AF7",
        (7U << 4) | (7U << 8),
        uart_host_get_gpioa_afrh() & ((0xFU << 4) | (0xFU << 8))
    );

    uart_putc('Z');
    expect_u32("uart putc DR", 'Z', uart_host_get_usart_dr() & 0xFFU);
}

static void test_uart_tx_wait_is_bounded(void)
{
    uart_host_reset_registers();
    uart_host_set_usart_sr(0U);

    uart_putc('X');

    expect_u32("uart bounded tx keeps DR", 0U, uart_host_get_usart_dr() & 0xFFU);
}

static void test_uart_wait_tx_complete(void)
{
    uart_host_reset_registers();
    uart_host_set_usart_sr(UART_HOST_USART_SR_TXE | UART_HOST_USART_SR_TC);
    expect_status(
        "uart tx complete ready",
        UART_OK,
        uart_wait_tx_complete()
    );

    uart_host_set_usart_sr(UART_HOST_USART_SR_TXE);
    expect_status(
        "uart tx complete bounded",
        UART_TIMEOUT,
        uart_wait_tx_complete()
    );
}

static void test_uart_rx_ready_and_getc(void)
{
    uint8_t byte = 0U;

    uart_host_reset_registers();
    expect_u32("rx not ready", 0U, uart_rx_ready());

    set_rx(UART_HOST_USART_SR_RXNE, 0x34U);
    expect_u32("rx ready", 1U, uart_rx_ready());
    expect_status(
        "rx getc ok",
        UART_OK,
        uart_getc_nonblocking(&byte)
    );
    expect_u32("rx byte", 0x34U, byte);
    expect_rx_cleared("rx getc clears RXNE");

    expect_status(
        "rx getc no data",
        UART_NO_DATA,
        uart_getc_nonblocking(&byte)
    );

    expect_status(
        "rx getc null",
        UART_ERR_INVALID_ARGUMENT,
        uart_getc_nonblocking(NULL)
    );
}

static void expect_uart_error(
    const char *name,
    uint32_t flag,
    uart_status_t expected
)
{
    uint8_t byte = 0U;

    set_rx(UART_HOST_USART_SR_RXNE | flag, 0xAAU);
    expect_u32("rx ready hides error", 0U, uart_rx_ready());
    expect_status(name, expected, uart_getc_nonblocking(&byte));
    expect_rx_cleared("error getc clears RX flags");
}

static void test_uart_rx_errors(void)
{
    uart_host_reset_registers();

    expect_uart_error(
        "overrun error",
        UART_HOST_USART_SR_ORE,
        UART_ERR_OVERRUN
    );
    expect_uart_error(
        "framing error",
        UART_HOST_USART_SR_FE,
        UART_ERR_FRAMING
    );
    expect_uart_error(
        "noise error",
        UART_HOST_USART_SR_NE,
        UART_ERR_NOISE
    );
    expect_uart_error(
        "parity error",
        UART_HOST_USART_SR_PE,
        UART_ERR_PARITY
    );
    expect_uart_error(
        "overrun priority",
        UART_HOST_USART_SR_ORE | UART_HOST_USART_SR_FE,
        UART_ERR_OVERRUN
    );
}

static void test_uart_timeout_read_flush_and_reader(void)
{
    byte_reader_t reader;
    uint8_t byte = 0U;
    uint8_t buffer[2] = {0U, 0U};
    size_t bytes_read = 99U;

    uart_host_reset_registers();
    expect_status(
        "uart timeout no data",
        UART_TIMEOUT,
        uart_getc_timeout(&byte, 2U)
    );
    expect_status(
        "uart timeout invalid before polling",
        UART_ERR_INVALID_ARGUMENT,
        uart_getc_timeout(NULL, 0U)
    );

    set_rx(UART_HOST_USART_SR_RXNE, 'Q');
    expect_status("uart timeout gets byte", UART_OK, uart_getc_timeout(&byte, 1U));
    expect_u32("uart timeout byte", 'Q', byte);

    set_rx(UART_HOST_USART_SR_RXNE, 'R');
    expect_status(
        "uart read partial timeout",
        UART_TIMEOUT,
        uart_read_timeout(buffer, sizeof(buffer), 1U, &bytes_read)
    );
    expect_size("uart read partial count", 1U, bytes_read);
    expect_u32("uart read partial byte", 'R', buffer[0]);

    set_rx(UART_HOST_USART_SR_RXNE, 'S');
    expect_status("uart flush data", UART_OK, uart_flush_rx());
    expect_rx_cleared("uart flush clears data");

    set_rx(UART_HOST_USART_SR_RXNE | UART_HOST_USART_SR_NE, 'T');
    expect_status("uart flush reports noise", UART_ERR_NOISE, uart_flush_rx());
    expect_rx_cleared("uart flush clears error");

    uart_byte_reader_init(&reader);
    set_rx(UART_HOST_USART_SR_RXNE, 'U');
    expect_reader_status(
        "uart byte reader ok",
        BYTE_READER_OK,
        byte_reader_getc_nonblocking(&reader, &byte)
    );
    expect_u32("uart byte reader byte", 'U', byte);

    set_rx(UART_HOST_USART_SR_RXNE | UART_HOST_USART_SR_FE, 'V');
    expect_reader_status(
        "uart byte reader maps error",
        BYTE_READER_ERR_IO,
        byte_reader_getc_nonblocking(&reader, &byte)
    );
}

int main(void)
{
    test_byte_reader_timeout_and_partial_read();
    test_uart_init_and_tx();
    test_uart_tx_wait_is_bounded();
    test_uart_wait_tx_complete();
    test_uart_rx_ready_and_getc();
    test_uart_rx_errors();
    test_uart_timeout_read_flush_and_reader();

    if (failures != 0) {
        printf("uart tests failed: %d\n", failures);
        return 1;
    }

    printf("uart tests passed\n");
    return 0;
}
