#include "platform.h"

#include "log.h"
#include "platform_audio.h"
#include "platform_health.h"
#include "uart.h"

#define REG32(a) (*(volatile uint32_t *)(a))
#define REG16(a) (*(volatile uint16_t *)(a))

#define CPUID REG32(0xE000ED00UL)
#define VTOR REG32(0xE000ED08UL)
#define SCB_CFSR REG32(0xE000ED28UL)
#define SCB_HFSR REG32(0xE000ED2CUL)
#define SCB_DFSR REG32(0xE000ED30UL)
#define SCB_AFSR REG32(0xE000ED3CUL)

#define SYST_CSR REG32(0xE000E010UL)
#define SYST_RVR REG32(0xE000E014UL)
#define SYST_CVR REG32(0xE000E018UL)
#define SYST_CALIB REG32(0xE000E01CUL)

#define NVIC_ISER0 REG32(0xE000E100UL)
#define NVIC_ISPR0 REG32(0xE000E200UL)
#define NVIC_IABR0 REG32(0xE000E300UL)

#define MPU_TYPE REG32(0xE000ED90UL)
#define MPU_CTRL REG32(0xE000ED94UL)
#define MPU_RNR REG32(0xE000ED98UL)

#define GPIOA_MODER REG32(0x40020000UL)
#define GPIOA_IDR REG32(0x40020010UL)

#define FLASH_ACR REG32(0x40023C00UL)
#define FLASH_SR REG32(0x40023C0CUL)
#define FLASH_CR REG32(0x40023C10UL)
#define FLASH_OPTCR REG32(0x40023C14UL)

#define RCC_CR REG32(0x40023800UL)
#define RCC_CFGR REG32(0x40023808UL)
#define RCC_CSR REG32(0x40023874UL)

#define UID0 REG32(0x1FFF7A10UL)
#define UID1 REG32(0x1FFF7A14UL)
#define UID2 REG32(0x1FFF7A18UL)
#define FLASH_SIZE_KB REG16(0x1FFF7A22UL)
#define DBGMCU_ID REG32(0xE0042000UL)

#define LOG_SRC_PLATFORM 0x0066U
#define LOG_EVENT_BOOT_RESET 1U
#define LOG_EVENT_BOOT_FAULT_RECORD 2U
#define LOG_EVENT_SELFTEST_FAILED 3U
#define LOG_EVENT_HEALTH_ACK 4U
#define LOG_EVENT_LED_TEST 5U
#define LOG_EVENT_EASTER_EGG 6U

static volatile uint32_t ticks;
static char line[80];
static uint32_t line_length;
static uint32_t boot_reset_csr;
static uint8_t mandatory_self_tests_passed;

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
    return eq(command, "registers pwr") ||
           eq(command, "registers syscfg");
}

static uint8_t mandatory_self_tests_pass(void)
{
    if (VTOR != PLATFORM_APP_BASE) {
        return 0U;
    }
    if ((uint32_t)FLASH_SIZE_KB == 0U) {
        return 0U;
    }
    return 1U;
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

static void print_health_status(void)
{
    uart_puts("health state ");
    uart_puts(platform_health_state_name(platform_health_get_state()));
    uart_puts(" automatic ");
    uart_puts(platform_health_state_name(platform_health_get_automatic_state()));
    uart_puts(" temporary ");
    uart_puts(platform_health_temporary_active() != 0U ? "yes" : "no");
    uart_puts(" led ");
    uart_put_hex32((uint32_t)platform_health_get_led_mask());
    newline();

    uart_puts("reset_csr ");
    uart_put_hex32(boot_reset_csr);
    uart_puts(" fault_record ");
    uart_puts(fault_valid() ? "valid" : "none");
    uart_puts(" self_tests ");
    uart_puts(mandatory_self_tests_passed != 0U ? "pass" : "fail");
    newline();
}

static void print_led_status(void)
{
    uart_puts("led state ");
    uart_puts(platform_health_state_name(platform_health_get_state()));
    uart_puts(" mask ");
    uart_put_hex32((uint32_t)platform_health_get_led_mask());
    uart_puts(" temporary ");
    uart_puts(platform_health_temporary_active() != 0U ? "yes" : "no");
    newline();
}

static void print_boot_status(void)
{
    uart_puts("boot status: Stage-0 verified launch assumed; no boot mailbox\n");
    uart_puts("reset_csr ");
    uart_put_hex32(boot_reset_csr);
    uart_puts(" health ");
    uart_puts(platform_health_state_name(platform_health_get_state()));
    newline();
}

static void print_help(void)
{
    uart_puts(
        "help version boot status device info device uid device flash-size "
        "device option-bytes reset cause clock show registers "
        "{rcc|gpio|nvic|scb|systick|mpu|flash|pwr|syscfg|dbgmcu} "
        "memory regions log {show|clear} fault {show|clear} "
        "health {status|acknowledge} led {status|test healthy|test degraded|"
        "test update|test recovery|test fault|test security|test stop} "
        "easteregg {knightrider|retro|stop} "
        "test {list|run gpio|run button|run clock|run ram} "
        "security status module list\n"
    );
}

static int handle_led_command(const char *command)
{
    if (eq(command, "led status")) {
        print_led_status();
    } else if (eq(command, "led test healthy")) {
        platform_health_start_led_test(PLATFORM_HEALTHY);
        log_write(LOG_INFO, LOG_SRC_PLATFORM, LOG_EVENT_LED_TEST, 0, 0U);
        uart_puts("ok\n");
    } else if (eq(command, "led test degraded")) {
        platform_health_start_led_test(PLATFORM_DEGRADED);
        log_write(LOG_INFO, LOG_SRC_PLATFORM, LOG_EVENT_LED_TEST, 0, 0U);
        uart_puts("ok\n");
    } else if (eq(command, "led test update")) {
        platform_health_start_led_test(PLATFORM_UPDATING);
        log_write(LOG_INFO, LOG_SRC_PLATFORM, LOG_EVENT_LED_TEST, 0, 0U);
        uart_puts("ok\n");
    } else if (eq(command, "led test recovery")) {
        platform_health_start_led_test(PLATFORM_RECOVERY);
        log_write(LOG_INFO, LOG_SRC_PLATFORM, LOG_EVENT_LED_TEST, 0, 0U);
        uart_puts("ok\n");
    } else if (eq(command, "led test fault")) {
        platform_health_start_led_test(PLATFORM_FAULT);
        log_write(LOG_INFO, LOG_SRC_PLATFORM, LOG_EVENT_LED_TEST, 0, 0U);
        uart_puts("ok\n");
    } else if (eq(command, "led test security")) {
        platform_health_start_led_test(PLATFORM_SECURITY_FAILURE);
        log_write(LOG_INFO, LOG_SRC_PLATFORM, LOG_EVENT_LED_TEST, 0, 0U);
        uart_puts("ok\n");
    } else if (eq(command, "led test stop")) {
        platform_health_stop_temporary();
        uart_puts("ok\n");
    } else {
        return 0;
    }
    return 1;
}

static int handle_easteregg_command(const char *command)
{
    if (eq(command, "easteregg knightrider")) {
        platform_health_start_easteregg_knightrider();
        log_write(LOG_INFO, LOG_SRC_PLATFORM, LOG_EVENT_EASTER_EGG, 0, 0U);
        uart_puts("ok\n");
    } else if (eq(command, "easteregg retro")) {
        if (platform_audio_start_retro() != 0U) {
            uart_puts("ok\n");
        } else {
            uart_puts("audio unavailable: no board buzzer defined\n");
        }
    } else if (eq(command, "easteregg stop")) {
        platform_health_stop_temporary();
        platform_audio_stop();
        uart_puts("ok\n");
    } else {
        return 0;
    }
    return 1;
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
        uart_puts(" cli ");
        uart_put_u32(PLATFORM_CLI_VERSION);
        newline();
    } else if (eq(command, "boot status")) {
        print_boot_status();
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
        print_snapshot("FLASH_OPTCR", FLASH_OPTCR);
    } else if (eq(command, "reset cause")) {
        print_snapshot("RCC_CSR_BOOT", boot_reset_csr);
        print_snapshot("RCC_CSR_NOW", RCC_CSR);
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
        print_snapshot("SCB_DFSR", SCB_DFSR);
        print_snapshot("SCB_AFSR", SCB_AFSR);
    } else if (eq(command, "registers systick")) {
        print_snapshot("SYST_CSR", SYST_CSR);
        print_snapshot("SYST_RVR", SYST_RVR);
        print_snapshot("SYST_CVR", SYST_CVR);
        print_snapshot("SYST_CALIB", SYST_CALIB);
    } else if (eq(command, "registers nvic")) {
        print_snapshot("NVIC_ISER0", NVIC_ISER0);
        print_snapshot("NVIC_ISPR0", NVIC_ISPR0);
        print_snapshot("NVIC_IABR0", NVIC_IABR0);
    } else if (eq(command, "registers mpu")) {
        print_snapshot("MPU_TYPE", MPU_TYPE);
        print_snapshot("MPU_CTRL", MPU_CTRL);
        print_snapshot("MPU_RNR", MPU_RNR);
    } else if (eq(command, "registers flash")) {
        print_snapshot("FLASH_ACR", FLASH_ACR);
        print_snapshot("FLASH_SR", FLASH_SR);
        print_snapshot("FLASH_CR", FLASH_CR);
        print_snapshot("FLASH_OPTCR", FLASH_OPTCR);
    } else if (is_unavailable_register_group(command)) {
        print_unavailable();
    } else if (eq(command, "memory regions")) {
        uart_puts(
            "bootloader 0x08000000-0x08007FFF\n"
            "manifest 0x08008000-0x0800805F\n"
            "signature 0x08008060-0x0800809F\n"
            "application 0x08008200-0x080FFFFF\n"
            "SRAM 0x20000000-0x2001FFFF\n"
        );
    } else if (eq(command, "log show")) {
        log_show();
    } else if (eq(command, "log clear")) {
        log_clear();
        uart_puts("ok\n");
    } else if (eq(command, "fault show")) {
        fault_show();
    } else if (eq(command, "fault clear")) {
        fault_clear();
        platform_health_acknowledge();
        uart_puts("ok\n");
    } else if (eq(command, "health status")) {
        print_health_status();
    } else if (eq(command, "health acknowledge")) {
        platform_health_acknowledge();
        log_write(LOG_INFO, LOG_SRC_PLATFORM, LOG_EVENT_HEALTH_ACK, 0, 0U);
        uart_puts("ok\n");
    } else if (handle_led_command(command)) {
        return;
    } else if (handle_easteregg_command(command)) {
        return;
    } else if (eq(command, "test list")) {
        uart_puts("gpio button clock ram\n");
    } else if (is_test_run_command(command)) {
        platform_health_start_led_test(PLATFORM_TEST_RUNNING);
        uart_puts("PASS (non-destructive placeholder)\n");
    } else if (eq(command, "security status")) {
        uart_puts(
            "designed for verified Stage-0; arbitrary memory disabled; "
            "module execution disabled; option-byte and RDP activation unavailable\n"
        );
    } else if (eq(command, "module list")) {
        uart_puts("no signed modules installed; execution disabled\n");
    } else {
        uart_puts("error: unknown command\n");
    }
}

void platform_init(void)
{
    uart_init();
    log_init();
    platform_health_init();

    boot_reset_csr = RCC_CSR;
    log_write(
        LOG_INFO,
        LOG_SRC_PLATFORM,
        LOG_EVENT_BOOT_RESET,
        &boot_reset_csr,
        (uint8_t)sizeof(boot_reset_csr)
    );

    if (fault_valid()) {
        log_write(LOG_ERROR, LOG_SRC_PLATFORM, LOG_EVENT_BOOT_FAULT_RECORD, 0, 0U);
    }

    mandatory_self_tests_passed = mandatory_self_tests_pass();
    if (mandatory_self_tests_passed == 0U) {
        log_write(LOG_ERROR, LOG_SRC_PLATFORM, LOG_EVENT_SELFTEST_FAILED, 0, 0U);
    }

    platform_health_apply_boot_policy(
        boot_reset_csr,
        fault_valid() ? 1U : 0U,
        mandatory_self_tests_passed,
        0U
    );
}

uint32_t platform_millis(void)
{
    return ticks;
}

void platform_tick(void)
{
    ++ticks;
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
    platform_tick();
    platform_health_service_tick();
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
