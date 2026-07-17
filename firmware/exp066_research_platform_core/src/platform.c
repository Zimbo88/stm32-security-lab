#include "platform.h"
#include "uart.h"
#include "log.h"

#define REG32(a) (*(volatile uint32_t *)(a))
#define REG16(a) (*(volatile uint16_t *)(a))

#define CPUID REG32(0xE000ED00UL)
#define VTOR REG32(0xE000ED08UL)
#define SCB_CFSR REG32(0xE000ED28UL)
#define SCB_HFSR REG32(0xE000ED2CUL)

#define GPIOA_MODER REG32(0x40020000UL)
#define GPIOA_IDR REG32(0x40020010UL)

#define RCC_CR REG32(0x40023800UL)
#define RCC_CFGR REG32(0x40023808UL)
#define RCC_CSR REG32(0x40023874UL)

#define UID0 REG32(0x1FFF7A10UL)
#define UID1 REG32(0x1FFF7A14UL)
#define UID2 REG32(0x1FFF7A18UL)
#define FLASH_SIZE_KB REG16(0x1FFF7A22UL)
#define DBGMCU_ID REG32(0xE0042000UL)

static volatile uint32_t ticks;
static char line[80];
static uint32_t line_length;

void platform_init(void)
{
    uart_init();
    log_init();
}

uint32_t platform_millis(void)
{
    return ticks;
}

void platform_tick(void)
{
    ++ticks;
}

static void newline(void)
{
    uart_puts("\n");
}

static int eq(const char *left, const char *right)
{
    while ((*left != '\0') && (*right != '\0') && (*left == *right)) {
        ++left;
        ++right;
    }
    return (*left == '\0') && (*right == '\0');
}

static int is_test_run_command(const char *command)
{
    return eq(command, "test run gpio") ||
           eq(command, "test run button") ||
           eq(command, "test run clock") ||
           eq(command, "test run ram");
}

static int is_unavailable_register_group(const char *command)
{
    return eq(command, "registers nvic") ||
           eq(command, "registers systick") ||
           eq(command, "registers mpu") ||
           eq(command, "registers flash") ||
           eq(command, "registers pwr") ||
           eq(command, "registers syscfg");
}

static void print_device_info(void)
{
    uint32_t msp;

    uart_puts("CPUID ");
    uart_put_hex32(CPUID);
    uart_puts(" DBGMCU ");
    uart_put_hex32(DBGMCU_ID);
    newline();

    uart_puts("UID ");
    uart_put_hex32(UID0);
    uart_putc(' ');
    uart_put_hex32(UID1);
    uart_putc(' ');
    uart_put_hex32(UID2);
    newline();

    uart_puts("flash_kb ");
    uart_put_u32((uint32_t)FLASH_SIZE_KB);
    newline();

    uart_puts("VTOR ");
    uart_put_hex32(VTOR);
    uart_puts(" MSP ");
    __asm volatile("mrs %0,msp" : "=r"(msp));
    uart_put_hex32(msp);
    newline();
}

static void print_snapshot(const char *name, uint32_t value)
{
    uart_puts(name);
    uart_puts("=");
    uart_put_hex32(value);
    newline();
}

static void print_unavailable(void)
{
    uart_puts("unavailable: group intentionally not sampled in EXP066\n");
}

static void print_help(void)
{
    uart_puts("help version device info device uid device flash-size device option-bytes reset cause clock show registers {rcc|gpio|nvic|scb|systick|mpu|flash|pwr|syscfg|dbgmcu} memory regions log {show|clear} fault {show|clear} test {list|run gpio|run button|run clock|run ram} security status module list\n");
}

static void dispatch_command(const char *command)
{
    if (eq(command, "help")) {
        print_help();
    } else if (eq(command, "version")) {
        uart_puts("platform ");
        uart_put_u32(PLATFORM_VERSION);
        uart_puts(" abi ");
        uart_put_u32(PLATFORM_ABI_VERSION);
        newline();
    } else if (eq(command, "device info")) {
        print_device_info();
    } else if (eq(command, "device uid")) {
        uart_put_hex32(UID0);
        uart_putc(' ');
        uart_put_hex32(UID1);
        uart_putc(' ');
        uart_put_hex32(UID2);
        newline();
    } else if (eq(command, "device flash-size")) {
        uart_put_u32((uint32_t)FLASH_SIZE_KB);
        uart_puts(" KiB\n");
    } else if (eq(command, "device option-bytes")) {
        uart_puts("read-only; option-byte register decoding unavailable\n");
    } else if (eq(command, "reset cause")) {
        print_snapshot("RCC_CSR", RCC_CSR);
    } else if (eq(command, "clock show")) {
        print_snapshot("RCC_CFGR", RCC_CFGR);
    } else if (eq(command, "registers rcc")) {
        print_snapshot("RCC_CR", RCC_CR);
        print_snapshot("RCC_CFGR", RCC_CFGR);
        print_snapshot("RCC_CSR", RCC_CSR);
    } else if (eq(command, "registers gpio")) {
        print_snapshot("GPIOA_MODER", GPIOA_MODER);
        print_snapshot("GPIOA_IDR", GPIOA_IDR);
    } else if (eq(command, "registers dbgmcu")) {
        print_snapshot("DBGMCU_IDCODE", DBGMCU_ID);
    } else if (eq(command, "registers scb")) {
        print_snapshot("SCB_CFSR", SCB_CFSR);
        print_snapshot("SCB_HFSR", SCB_HFSR);
    } else if (is_unavailable_register_group(command)) {
        print_unavailable();
    } else if (eq(command, "memory regions")) {
        uart_puts("bootloader 0x08000000-0x08007FFF\nmanifest 0x08008000-0x0800805F\nsignature 0x08008060-0x0800809F\napplication 0x08008200-0x080FFFFF\nSRAM 0x20000000-0x2001FFFF\n");
    } else if (eq(command, "log show")) {
        log_show();
    } else if (eq(command, "log clear")) {
        log_clear();
        uart_puts("ok\n");
    } else if (eq(command, "fault show")) {
        fault_show();
    } else if (eq(command, "fault clear")) {
        fault_clear();
        uart_puts("ok\n");
    } else if (eq(command, "test list")) {
        uart_puts("gpio button clock ram\n");
    } else if (is_test_run_command(command)) {
        uart_puts("PASS (non-destructive placeholder)\n");
    } else if (eq(command, "security status")) {
        uart_puts("designed for verified Stage-0; arbitrary memory disabled; module execution disabled; option-byte and RDP activation unavailable\n");
    } else if (eq(command, "module list")) {
        uart_puts("no signed modules installed; execution disabled\n");
    } else {
        uart_puts("error: unknown command\n");
    }
}

void platform_cli_poll(void)
{
    char c;

    if (!uart_getc_nonblocking(&c)) {
        return;
    }

    if ((c == '\r') || (c == '\n')) {
        line[line_length] = '\0';
        if (line_length != 0U) {
            dispatch_command(line);
        }
        line_length = 0U;
        uart_puts("rp> ");
    } else if (c == '\b') {
        if (line_length != 0U) {
            --line_length;
        }
    } else if (line_length < (sizeof(line) - 1U)) {
        line[line_length] = c;
        ++line_length;
    }
}

void platform_idle(void)
{
    platform_cli_poll();
}

int main(void)
{
    platform_init();
    uart_puts("EXP066 RESEARCH PLATFORM\nrp> ");
    for (;;) {
        platform_idle();
    }
}
