#include "uart.h"

#include "board.h"
#include "board_clock.h"

#define UART_BAUD_115200            115200UL
#define UART_FLUSH_RX_MAX_DRAINS    1024UL
#define UART_TX_READY_MAX_POLLS     1000000UL

#define RCC_AHB1ENR_GPIOAEN         (1UL << 0)
#define RCC_APB2ENR_USART1EN        (1UL << 4)

#define USART_SR_PE                 (1UL << 0)
#define USART_SR_FE                 (1UL << 1)
#define USART_SR_NE                 (1UL << 2)
#define USART_SR_ORE                (1UL << 3)
#define USART_SR_RXNE               (1UL << 5)
#define USART_SR_TXE                (1UL << 7)
#define USART_SR_RX_ERROR_MASK \
    (USART_SR_PE | USART_SR_FE | USART_SR_NE | USART_SR_ORE)

#define USART_CR1_UE                (1UL << 13)
#define USART_CR1_TE                (1UL << 3)
#define USART_CR1_RE                (1UL << 2)

typedef struct {
    volatile uint32_t *rcc_ahb1enr;
    volatile uint32_t *rcc_apb2enr;
    volatile uint32_t *gpioa_moder;
    volatile uint32_t *gpioa_otyper;
    volatile uint32_t *gpioa_ospeedr;
    volatile uint32_t *gpioa_pupdr;
    volatile uint32_t *gpioa_afrh;
    volatile uint32_t *usart_sr;
    volatile uint32_t *usart_dr;
    volatile uint32_t *usart_brr;
    volatile uint32_t *usart_cr1;
    volatile uint32_t *usart_cr2;
    volatile uint32_t *usart_cr3;
} uart_registers_t;

#ifdef UART_HOST_TEST
static volatile uint32_t uart_host_rcc_ahb1enr;
static volatile uint32_t uart_host_rcc_apb2enr;
static volatile uint32_t uart_host_gpioa_moder;
static volatile uint32_t uart_host_gpioa_otyper;
static volatile uint32_t uart_host_gpioa_ospeedr;
static volatile uint32_t uart_host_gpioa_pupdr;
static volatile uint32_t uart_host_gpioa_afrh;
static volatile uint32_t uart_host_usart_sr;
static volatile uint32_t uart_host_usart_dr;
static volatile uint32_t uart_host_usart_brr;
static volatile uint32_t uart_host_usart_cr1;
static volatile uint32_t uart_host_usart_cr2;
static volatile uint32_t uart_host_usart_cr3;

static const uart_registers_t uart1 = {
    &uart_host_rcc_ahb1enr,
    &uart_host_rcc_apb2enr,
    &uart_host_gpioa_moder,
    &uart_host_gpioa_otyper,
    &uart_host_gpioa_ospeedr,
    &uart_host_gpioa_pupdr,
    &uart_host_gpioa_afrh,
    &uart_host_usart_sr,
    &uart_host_usart_dr,
    &uart_host_usart_brr,
    &uart_host_usart_cr1,
    &uart_host_usart_cr2,
    &uart_host_usart_cr3
};
#else
static volatile uint32_t *const rcc_ahb1enr =
    (volatile uint32_t *)0x40023830UL;
static volatile uint32_t *const rcc_apb2enr =
    (volatile uint32_t *)0x40023844UL;
static volatile uint32_t *const gpioa_moder =
    (volatile uint32_t *)0x40020000UL;
static volatile uint32_t *const gpioa_otyper =
    (volatile uint32_t *)0x40020004UL;
static volatile uint32_t *const gpioa_ospeedr =
    (volatile uint32_t *)0x40020008UL;
static volatile uint32_t *const gpioa_pupdr =
    (volatile uint32_t *)0x4002000CUL;
static volatile uint32_t *const gpioa_afrh =
    (volatile uint32_t *)0x40020024UL;
static volatile uint32_t *const usart1_sr =
    (volatile uint32_t *)0x40011000UL;
static volatile uint32_t *const usart1_dr =
    (volatile uint32_t *)0x40011004UL;
static volatile uint32_t *const usart1_brr =
    (volatile uint32_t *)0x40011008UL;
static volatile uint32_t *const usart1_cr1 =
    (volatile uint32_t *)0x4001100CUL;
static volatile uint32_t *const usart1_cr2 =
    (volatile uint32_t *)0x40011010UL;
static volatile uint32_t *const usart1_cr3 =
    (volatile uint32_t *)0x40011014UL;

static const uart_registers_t uart1 = {
    rcc_ahb1enr,
    rcc_apb2enr,
    gpioa_moder,
    gpioa_otyper,
    gpioa_ospeedr,
    gpioa_pupdr,
    gpioa_afrh,
    usart1_sr,
    usart1_dr,
    usart1_brr,
    usart1_cr1,
    usart1_cr2,
    usart1_cr3
};
#endif

static uint32_t uart_brr_from_clock(uint32_t pclk_hz)
{
    return (pclk_hz + (UART_BAUD_115200 / 2UL)) / UART_BAUD_115200;
}

static uint32_t uart_read_reg(volatile uint32_t *reg)
{
    return *reg;
}

static void uart_write_reg(volatile uint32_t *reg, uint32_t value)
{
    *reg = value;
}

static void uart_set_bits(volatile uint32_t *reg, uint32_t mask)
{
    uart_write_reg(reg, uart_read_reg(reg) | mask);
}

static void uart_clear_bits(volatile uint32_t *reg, uint32_t mask)
{
    uart_write_reg(reg, uart_read_reg(reg) & ~mask);
}

static uint32_t uart_read_sr(void)
{
    return uart_read_reg(uart1.usart_sr);
}

static uint32_t uart_read_dr(void)
{
    const uint32_t value = uart_read_reg(uart1.usart_dr);

#ifdef UART_HOST_TEST
    uart_write_reg(
        uart1.usart_sr,
        uart_read_reg(uart1.usart_sr) &
            ~(USART_SR_RXNE | USART_SR_RX_ERROR_MASK)
    );
#endif

    return value;
}

static void uart_write_dr(uint32_t value)
{
    uart_write_reg(uart1.usart_dr, value);
}

static void uart_enable_peripheral_clocks(void)
{
    uart_set_bits(uart1.rcc_ahb1enr, RCC_AHB1ENR_GPIOAEN);
    uart_set_bits(uart1.rcc_apb2enr, RCC_APB2ENR_USART1EN);
    (void)uart_read_reg(uart1.rcc_ahb1enr);
    (void)uart_read_reg(uart1.rcc_apb2enr);
}

static void uart_configure_gpio_pa9_pa10(void)
{
    uart_clear_bits(uart1.gpioa_moder, (3UL << 18) | (3UL << 20));
    uart_set_bits(uart1.gpioa_moder, (2UL << 18) | (2UL << 20));

    uart_clear_bits(uart1.gpioa_otyper, (1UL << 9) | (1UL << 10));
    uart_set_bits(uart1.gpioa_ospeedr, (2UL << 18) | (2UL << 20));

    uart_clear_bits(uart1.gpioa_pupdr, (3UL << 18) | (3UL << 20));
    uart_set_bits(uart1.gpioa_pupdr, 1UL << 20);

    uart_clear_bits(uart1.gpioa_afrh, (0xFUL << 4) | (0xFUL << 8));
    uart_set_bits(uart1.gpioa_afrh, (7UL << 4) | (7UL << 8));
}

static void uart_configure_usart1(uint32_t pclk_hz)
{
    if (pclk_hz == 0UL) {
        pclk_hz = BOARD_HSI_HZ;
    }

    uart_write_reg(uart1.usart_cr1, 0U);
    uart_write_reg(uart1.usart_cr2, 0U);
    uart_write_reg(uart1.usart_cr3, 0U);
    uart_write_reg(uart1.usart_brr, uart_brr_from_clock(pclk_hz));
    uart_write_reg(
        uart1.usart_cr1,
        USART_CR1_UE | USART_CR1_TE | USART_CR1_RE
    );
}

static uart_status_t uart_error_from_sr(uint32_t sr)
{
    if ((sr & USART_SR_ORE) != 0UL) {
        return UART_ERR_OVERRUN;
    }
    if ((sr & USART_SR_FE) != 0UL) {
        return UART_ERR_FRAMING;
    }
    if ((sr & USART_SR_NE) != 0UL) {
        return UART_ERR_NOISE;
    }
    if ((sr & USART_SR_PE) != 0UL) {
        return UART_ERR_PARITY;
    }

    return UART_OK;
}

static void uart_clear_rx_condition(void)
{
    (void)uart_read_sr();
    (void)uart_read_dr();
}

void uart_init_115200(uint32_t pclk_hz)
{
    uart_enable_peripheral_clocks();
    uart_configure_gpio_pa9_pa10();
    uart_configure_usart1(pclk_hz);
}

void uart_init(void)
{
    uart_init_115200(board_clock_get_sysclk_hz());
}

void uart_init_115200_hsi16(void)
{
    uart_init_115200(BOARD_HSI_HZ);
}

void uart_putc(char c)
{
    uint32_t polls = UART_TX_READY_MAX_POLLS;

    while ((uart_read_sr() & USART_SR_TXE) == 0U) {
        if (polls == 0UL) {
            return;
        }
        --polls;
    }
    uart_write_dr((uint32_t)(uint8_t)c);
}

void uart_puts(const char *s)
{
    while (*s != '\0') {
        if (*s == '\n') {
            uart_putc('\r');
        }
        uart_putc(*s++);
    }
}

void uart_put_hex32(uint32_t value)
{
    static const char digits[] = "0123456789ABCDEF";
    uart_puts("0x");
    for (int shift = 28; shift >= 0; shift -= 4) {
        uart_putc(digits[(value >> (uint32_t)shift) & 0xFUL]);
    }
}

void uart_put_u32(uint32_t value)
{
    char buffer[10];
    uint32_t count = 0U;

    if (value == 0U) {
        uart_putc('0');
        return;
    }

    while (value != 0U) {
        buffer[count++] = (char)('0' + (value % 10U));
        value /= 10U;
    }

    while (count != 0U) {
        uart_putc(buffer[--count]);
    }
}

uint8_t uart_rx_ready(void)
{
    const uint32_t sr = uart_read_sr();

    if ((sr & USART_SR_RX_ERROR_MASK) != 0UL) {
        return 0U;
    }

    return ((sr & USART_SR_RXNE) != 0UL) ? 1U : 0U;
}

uart_status_t uart_getc_nonblocking(uint8_t *byte)
{
    const uint32_t sr = uart_read_sr();
    const uart_status_t error_status = uart_error_from_sr(sr);

    if (byte == NULL) {
        return UART_ERR_INVALID_ARGUMENT;
    }

    if (error_status != UART_OK) {
        uart_clear_rx_condition();
        return error_status;
    }

    if ((sr & USART_SR_RXNE) == 0UL) {
        return UART_NO_DATA;
    }

    *byte = (uint8_t)uart_read_dr();
    return UART_OK;
}

uart_status_t uart_getc_timeout(uint8_t *byte, uint32_t timeout_polls)
{
    if (byte == NULL) {
        return UART_ERR_INVALID_ARGUMENT;
    }

    while (timeout_polls != 0UL) {
        const uart_status_t status = uart_getc_nonblocking(byte);

        if (status != UART_NO_DATA) {
            return status;
        }

        --timeout_polls;
    }

    return UART_TIMEOUT;
}

uart_status_t uart_read_timeout(
    uint8_t *buffer,
    size_t length,
    uint32_t per_byte_timeout_polls,
    size_t *bytes_read
)
{
    size_t count = 0U;

    if (bytes_read != NULL) {
        *bytes_read = 0U;
    }

    if ((length != 0U) && (buffer == NULL)) {
        return UART_ERR_INVALID_ARGUMENT;
    }

    while (count < length) {
        const uart_status_t status = uart_getc_timeout(
            &buffer[count],
            per_byte_timeout_polls
        );

        if (status != UART_OK) {
            if (bytes_read != NULL) {
                *bytes_read = count;
            }
            return status;
        }

        ++count;
    }

    if (bytes_read != NULL) {
        *bytes_read = count;
    }

    return UART_OK;
}

uart_status_t uart_flush_rx(void)
{
    uart_status_t first_error = UART_OK;

    for (uint32_t drains = 0U; drains < UART_FLUSH_RX_MAX_DRAINS; ++drains) {
        const uint32_t sr = uart_read_sr();
        const uart_status_t error_status = uart_error_from_sr(sr);

        if ((error_status == UART_OK) && ((sr & USART_SR_RXNE) == 0UL)) {
            return first_error;
        }

        if ((error_status != UART_OK) && (first_error == UART_OK)) {
            first_error = error_status;
        }

        uart_clear_rx_condition();
    }

    return UART_TIMEOUT;
}

static byte_reader_status_t uart_byte_reader_getc(
    void *context,
    uint8_t *byte
)
{
    (void)context;

    switch (uart_getc_nonblocking(byte)) {
    case UART_OK:                   return BYTE_READER_OK;
    case UART_NO_DATA:              return BYTE_READER_NO_DATA;
    case UART_ERR_INVALID_ARGUMENT: return BYTE_READER_ERR_INVALID_ARGUMENT;
    case UART_TIMEOUT:              return BYTE_READER_TIMEOUT;
    case UART_ERR_OVERRUN:
    case UART_ERR_FRAMING:
    case UART_ERR_NOISE:
    case UART_ERR_PARITY:
    default:
        return BYTE_READER_ERR_IO;
    }
}

void uart_byte_reader_init(byte_reader_t *reader)
{
    byte_reader_init(reader, uart_byte_reader_getc, NULL);
}

const char *uart_status_text(uart_status_t status)
{
    switch (status) {
    case UART_OK:                   return "OK";
    case UART_NO_DATA:              return "NO DATA";
    case UART_TIMEOUT:              return "TIMEOUT";
    case UART_ERR_INVALID_ARGUMENT: return "INVALID ARGUMENT";
    case UART_ERR_OVERRUN:          return "OVERRUN";
    case UART_ERR_FRAMING:          return "FRAMING";
    case UART_ERR_NOISE:            return "NOISE";
    case UART_ERR_PARITY:           return "PARITY";
    default:                        return "UNKNOWN";
    }
}

#ifdef UART_HOST_TEST
void uart_host_reset_registers(void)
{
    uart_host_rcc_ahb1enr = 0U;
    uart_host_rcc_apb2enr = 0U;
    uart_host_gpioa_moder = 0U;
    uart_host_gpioa_otyper = 0U;
    uart_host_gpioa_ospeedr = 0U;
    uart_host_gpioa_pupdr = 0U;
    uart_host_gpioa_afrh = 0U;
    uart_host_usart_sr = USART_SR_TXE;
    uart_host_usart_dr = 0U;
    uart_host_usart_brr = 0U;
    uart_host_usart_cr1 = 0U;
    uart_host_usart_cr2 = 0U;
    uart_host_usart_cr3 = 0U;
}

void uart_host_set_usart_sr(uint32_t value)
{
    uart_host_usart_sr = value;
}

void uart_host_set_usart_dr(uint32_t value)
{
    uart_host_usart_dr = value;
}

uint32_t uart_host_get_rcc_ahb1enr(void) { return uart_host_rcc_ahb1enr; }
uint32_t uart_host_get_rcc_apb2enr(void) { return uart_host_rcc_apb2enr; }
uint32_t uart_host_get_gpioa_moder(void) { return uart_host_gpioa_moder; }
uint32_t uart_host_get_gpioa_otyper(void) { return uart_host_gpioa_otyper; }
uint32_t uart_host_get_gpioa_ospeedr(void) { return uart_host_gpioa_ospeedr; }
uint32_t uart_host_get_gpioa_pupdr(void) { return uart_host_gpioa_pupdr; }
uint32_t uart_host_get_gpioa_afrh(void) { return uart_host_gpioa_afrh; }
uint32_t uart_host_get_usart_sr(void) { return uart_host_usart_sr; }
uint32_t uart_host_get_usart_dr(void) { return uart_host_usart_dr; }
uint32_t uart_host_get_usart_brr(void) { return uart_host_usart_brr; }
uint32_t uart_host_get_usart_cr1(void) { return uart_host_usart_cr1; }
uint32_t uart_host_get_usart_cr2(void) { return uart_host_usart_cr2; }
uint32_t uart_host_get_usart_cr3(void) { return uart_host_usart_cr3; }
#endif
