#include "platform.h"

#include "experiment_telemetry.h"
#include "log.h"
#include "platform_confirmation.h"
#include "platform_audio.h"
#include "platform_health.h"
#include "runtime_monitor.h"
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
#define RCC_CSR_RMVF (1UL << 24)
#define RCC_CSR_BORRSTF (1UL << 25)
#define RCC_CSR_PINRSTF (1UL << 26)
#define RCC_CSR_PORRSTF (1UL << 27)
#define RCC_CSR_SFTRSTF (1UL << 28)
#define RCC_CSR_IWDGRSTF (1UL << 29)
#define RCC_CSR_WWDGRSTF (1UL << 30)
#define RCC_CSR_LPWRRSTF (1UL << 31)

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
#define LOG_EVENT_CONFIRMATION 7U

#ifndef PLATFORM_CONFIRMATION_LAB_BOOT_DELAY_TICKS
#define PLATFORM_CONFIRMATION_LAB_BOOT_DELAY_TICKS 0UL
#endif

static volatile uint32_t ticks;
static char line[80];
static uint32_t line_length;
static uint32_t boot_reset_csr;
static uint8_t mandatory_self_tests_passed;
static uint8_t early_platform_init_done;
static uint8_t critical_initialization_failure;
static uint8_t stable_execution_point_reached;
static uint8_t confirmation_terminal;

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
    if ((uint32_t)FLASH_SIZE_KB != STM32F429_FLASH_SIZE_KIB) {
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

static void print_address_range(const char *name, uint32_t start, uint32_t end)
{
    uart_puts(name);
    uart_putc(' ');
    uart_put_hex32(start);
    uart_putc('-');
    uart_put_hex32(end - 1UL);
    newline();
}

static void print_unavailable(void)
{
    uart_puts("unavailable: group intentionally not sampled in EXP066\n");
}

static uint8_t restricted_diagnostics_allowed(void)
{
    return runtime_monitor_diagnostic_allowed(RSM_INFO_RESTRICTED);
}

static void print_restricted_denied(void)
{
    runtime_monitor_record_diagnostic_denial(RSM_INFO_RESTRICTED);
    uart_puts("denied: restricted diagnostic disabled\n");
}

static uint8_t require_restricted_diagnostics(void)
{
    if (restricted_diagnostics_allowed() != 0U) {
        return 1U;
    }
    print_restricted_denied();
    return 0U;
}

static void print_reset_flag(const char *name, uint32_t mask)
{
    uart_puts(name);
    uart_puts((boot_reset_csr & mask) != 0UL ? "YES\n" : "NO\n");
}

static void print_device_info(void)
{
    const uint32_t dbgmcu = DBGMCU_ID;
    const uint32_t fingerprint =
        rsm_uid_fingerprint_words(UID0, UID1, UID2);

    uart_puts("family STM32F429 device_id ");
    uart_put_hex32(dbgmcu & 0x0FFFUL);
    uart_puts(" revision ");
    uart_put_hex32((dbgmcu >> 16U) & 0xFFFFUL);
    newline();

    uart_puts("CPUID ");
    uart_put_hex32(CPUID);
    newline();

    uart_puts("uid_fingerprint ");
    uart_put_hex32(fingerprint);
    newline();

    uart_puts("flash_kb ");
    uart_put_u32((uint32_t)FLASH_SIZE_KB);
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

static void print_fault_public_summary(void)
{
    uart_puts("fault: ");
    uart_puts(fault_valid() ? "valid restricted_details=denied" : "none");
    newline();
}

static void print_log_public_summary(void)
{
    uart_puts("records=");
    uart_put_u32(log_count());
    uart_puts(" dropped=");
    uart_put_u32(log_dropped());
    uart_puts(" last_sequence=");
    uart_put_u32(log_last_sequence());
    uart_puts(" restricted_details=denied\n");
}

static void print_confirmation_public_status(void)
{
    platform_confirmation_snapshot_t snapshot;

    platform_confirmation_snapshot(&snapshot);
    uart_puts("confirmation ");
    uart_puts(platform_confirmation_status_text(snapshot.last_status));
    uart_puts(" running ");
    uart_puts(rsm_slot_name(snapshot.running_slot));
    uart_puts(" firmware_version ");
    if (snapshot.image_version == 0UL) {
        uart_puts("unavailable");
    } else {
        uart_put_u32(snapshot.image_version);
    }
    newline();
}

static void print_confirmation_status(void)
{
    platform_confirmation_snapshot_t snapshot;

    platform_confirmation_snapshot(&snapshot);
    uart_puts("confirmation ");
    uart_puts(platform_confirmation_status_text(snapshot.last_status));
    uart_puts(" running ");
    uart_put_hex32(snapshot.running_slot);
    uart_puts(" confirmed ");
    uart_put_hex32(snapshot.confirmed_slot);
    uart_puts(" candidate ");
    uart_put_hex32(snapshot.candidate_slot);
    uart_puts(" metadata_state ");
    uart_put_hex32(snapshot.metadata_state);
    uart_puts(" attempts ");
    uart_put_u32(snapshot.remaining_trial_attempts);
    uart_puts(" already ");
    uart_puts(snapshot.already_confirmed != 0U ? "yes" : "no");
    newline();
}

static void print_reset_decoded(void)
{
    uart_puts("reset_cause=");
    uart_puts(rsm_reset_cause_name(boot_reset_csr));
    newline();
    print_reset_flag("brownout_reset=", RCC_CSR_BORRSTF);
    print_reset_flag("pin_reset=", RCC_CSR_PINRSTF);
    print_reset_flag("power_on_reset=", RCC_CSR_PORRSTF);
    print_reset_flag("software_reset=", RCC_CSR_SFTRSTF);
    print_reset_flag("independent_watchdog_reset=", RCC_CSR_IWDGRSTF);
    print_reset_flag("window_watchdog_reset=", RCC_CSR_WWDGRSTF);
    print_reset_flag("low_power_reset=", RCC_CSR_LPWRRSTF);
}

static void print_clock_public_summary(void)
{
    uint32_t hclk = 0UL;

    uart_puts("clock_source=");
    uart_puts(rsm_clock_source_name_from_cfgr(RCC_CFGR));
    newline();
    uart_puts("hclk_hz=");
    if (rsm_hclk_hz_from_cfgr(RCC_CFGR, &hclk) != 0U) {
        uart_put_u32(hclk);
    } else {
        uart_puts("unavailable");
    }
    newline();
}

static void print_help(void)
{
    uart_puts(
        "help version boot status device info device uid device flash-size "
        "device option-bytes reset cause clock show registers "
        "{rcc|gpio|nvic|scb|systick|mpu|flash|pwr|syscfg|dbgmcu} "
        "memory regions log {show|clear} fault {show|clear} "
        "health {status|acknowledge} confirmation status telemetry show "
        "rsm status rsm status public rsm status restricted "
        "reset decoded led {status|test healthy|test degraded|"
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
        if (restricted_diagnostics_allowed() != 0U) {
            uart_put_hex32(UID0);
            uart_putc(' ');
            uart_put_hex32(UID1);
            uart_putc(' ');
            uart_put_hex32(UID2);
            newline();
        } else {
            uart_puts("uid_fingerprint ");
            uart_put_hex32(rsm_uid_fingerprint_words(UID0, UID1, UID2));
            uart_puts(" full_uid=restricted\n");
        }
    } else if (eq(command, "device flash-size")) {
        uart_put_u32((uint32_t)FLASH_SIZE_KB);
        uart_puts(" KiB\n");
    } else if (eq(command, "device option-bytes")) {
        if (require_restricted_diagnostics() != 0U) {
            print_snapshot("FLASH_OPTCR", FLASH_OPTCR);
        }
    } else if (eq(command, "reset cause")) {
        uart_puts("reset_cause ");
        uart_puts(rsm_reset_cause_name(boot_reset_csr));
        newline();
    } else if (eq(command, "reset decoded")) {
        print_reset_decoded();
    } else if (eq(command, "clock show")) {
        print_clock_public_summary();
    } else if (eq(command, "registers rcc")) {
        if (require_restricted_diagnostics() != 0U) {
            print_snapshot("RCC_CR", RCC_CR);
            print_snapshot("RCC_CFGR", RCC_CFGR);
            print_snapshot("RCC_CSR", RCC_CSR);
        }
    } else if (eq(command, "registers gpio")) {
        if (require_restricted_diagnostics() != 0U) {
            print_snapshot("GPIOA_MODER", GPIOA_MODER);
            print_snapshot("GPIOA_IDR", GPIOA_IDR);
        }
    } else if (eq(command, "registers dbgmcu")) {
        if (require_restricted_diagnostics() != 0U) {
            print_snapshot("DBGMCU_IDCODE", DBGMCU_ID);
        }
    } else if (eq(command, "registers scb")) {
        if (require_restricted_diagnostics() != 0U) {
            print_snapshot("SCB_CFSR", SCB_CFSR);
            print_snapshot("SCB_HFSR", SCB_HFSR);
            print_snapshot("SCB_DFSR", SCB_DFSR);
            print_snapshot("SCB_AFSR", SCB_AFSR);
        }
    } else if (eq(command, "registers systick")) {
        if (require_restricted_diagnostics() != 0U) {
            print_snapshot("SYST_CSR", SYST_CSR);
            print_snapshot("SYST_RVR", SYST_RVR);
            print_snapshot("SYST_CVR", SYST_CVR);
            print_snapshot("SYST_CALIB", SYST_CALIB);
        }
    } else if (eq(command, "registers nvic")) {
        if (require_restricted_diagnostics() != 0U) {
            print_snapshot("NVIC_ISER0", NVIC_ISER0);
            print_snapshot("NVIC_ISPR0", NVIC_ISPR0);
            print_snapshot("NVIC_IABR0", NVIC_IABR0);
        }
    } else if (eq(command, "registers mpu")) {
        if (require_restricted_diagnostics() != 0U) {
            print_snapshot("MPU_TYPE", MPU_TYPE);
            print_snapshot("MPU_CTRL", MPU_CTRL);
            print_snapshot("MPU_RNR", MPU_RNR);
        }
    } else if (eq(command, "registers flash")) {
        if (require_restricted_diagnostics() != 0U) {
            print_snapshot("FLASH_ACR", FLASH_ACR);
            print_snapshot("FLASH_SR", FLASH_SR);
            print_snapshot("FLASH_CR", FLASH_CR);
            print_snapshot("FLASH_OPTCR", FLASH_OPTCR);
        }
    } else if (is_unavailable_register_group(command)) {
        print_unavailable();
    } else if (eq(command, "memory regions")) {
        if (require_restricted_diagnostics() != 0U) {
            print_address_range(
                "bootloader",
                STM32F429_BOOTLOADER_BASE,
                STM32F429_BOOTLOADER_END
            );
            print_address_range(
                "manifest",
                STM32F429_SIGNED_IMAGE_BASE,
                STM32F429_SIGNATURE_BASE
            );
            print_address_range(
                "signature",
                STM32F429_SIGNATURE_BASE,
                STM32F429_SIGNATURE_BASE + STM32F429_SIGNED_SIGNATURE_SIZE
            );
            print_address_range(
                "application",
                STM32F429_APPLICATION_BASE,
                STM32F429_APPLICATION_FLASH_END
            );
            print_address_range(
                "SRAM",
                STM32F429_MAIN_SRAM_BASE,
                STM32F429_MAIN_SRAM_SUPPORTED_END
            );
        }
    } else if (eq(command, "log show")) {
        if (restricted_diagnostics_allowed() != 0U) {
            log_show();
        } else {
            runtime_monitor_record_diagnostic_denial(RSM_INFO_RESTRICTED);
            print_log_public_summary();
        }
    } else if (eq(command, "log clear")) {
        if (require_restricted_diagnostics() != 0U) {
            log_clear();
            uart_puts("ok\n");
        }
    } else if (eq(command, "fault show")) {
        if (restricted_diagnostics_allowed() != 0U) {
            fault_show();
        } else {
            runtime_monitor_record_diagnostic_denial(RSM_INFO_RESTRICTED);
            print_fault_public_summary();
        }
    } else if (eq(command, "fault clear")) {
        if (require_restricted_diagnostics() != 0U) {
            fault_clear();
            platform_health_acknowledge();
            uart_puts("ok\n");
        }
    } else if (eq(command, "health status")) {
        print_health_status();
    } else if (eq(command, "confirmation status")) {
        if (restricted_diagnostics_allowed() != 0U) {
            print_confirmation_status();
        } else {
            print_confirmation_public_status();
        }
    } else if (eq(command, "telemetry show")) {
        if (require_restricted_diagnostics() != 0U) {
            experiment_telemetry_print();
        }
    } else if (eq(command, "rsm status") ||
               eq(command, "rsm status public")) {
        runtime_monitor_print_public_status();
    } else if (eq(command, "rsm status restricted")) {
        (void)runtime_monitor_print_restricted_status();
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
            "module execution disabled; option-byte and RDP activation unavailable; "
            "restricted diagnostics disabled by default; secret diagnostics never available\n"
        );
    } else if (eq(command, "module list")) {
        uart_puts("no signed modules installed; execution disabled\n");
    } else {
        uart_puts("error: unknown command\n");
    }
}

void platform_init(void)
{
    rsm_config_t rsm_config;

    uart_init();
    log_init();
    platform_health_init();

    boot_reset_csr = RCC_CSR;
    experiment_telemetry_init(boot_reset_csr);
    log_write(
        LOG_INFO,
        LOG_SRC_PLATFORM,
        LOG_EVENT_BOOT_RESET,
        &boot_reset_csr,
        (uint8_t)sizeof(boot_reset_csr)
    );
    RCC_CSR |= RCC_CSR_RMVF;

    if (fault_valid()) {
        log_write(LOG_ERROR, LOG_SRC_PLATFORM, LOG_EVENT_BOOT_FAULT_RECORD, 0, 0U);
    }

    mandatory_self_tests_passed = mandatory_self_tests_pass();
    if (mandatory_self_tests_passed == 0U) {
        critical_initialization_failure = 1U;
        log_write(LOG_ERROR, LOG_SRC_PLATFORM, LOG_EVENT_SELFTEST_FAILED, 0, 0U);
    }
    early_platform_init_done = 1U;

    platform_health_apply_boot_policy(
        boot_reset_csr,
        fault_valid() ? 1U : 0U,
        mandatory_self_tests_passed,
        0U
    );
    experiment_telemetry_set_boot_policy_result(
        (uint32_t)platform_health_get_automatic_state()
    );

    rsm_config.reset_csr = boot_reset_csr;
    rsm_config.boot_self_tests_passed = mandatory_self_tests_passed;
    (void)runtime_monitor_init(&rsm_config);
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
    platform_confirmation_health_t health;
    platform_confirmation_status_t confirmation;

    platform_tick();
    platform_health_service_tick();
    runtime_monitor_periodic();
    platform_cli_poll();

#if PLATFORM_CONFIRMATION_LAB_BOOT_DELAY_TICKS == 0UL
    stable_execution_point_reached = 1U;
#else
    if (ticks >= PLATFORM_CONFIRMATION_LAB_BOOT_DELAY_TICKS) {
        stable_execution_point_reached = 1U;
    }
#endif

    if (confirmation_terminal == 0U) {
        health.early_platform_init_done = early_platform_init_done;
        health.running_slot_identified = 0U;
        health.core_self_checks_passed = mandatory_self_tests_passed;
        health.critical_initialization_failure =
            critical_initialization_failure;
        health.stable_execution_point_reached =
            stable_execution_point_reached;
        health.metadata_allows_confirmation = 0U;

        confirmation = platform_confirmation_service(VTOR, &health);
        if (confirmation != PLATFORM_CONFIRMATION_NOT_HEALTHY) {
            confirmation_terminal = 1U;
            log_write(
                confirmation == PLATFORM_CONFIRMATION_OK
                    ? LOG_INFO
                    : LOG_ERROR,
                LOG_SRC_PLATFORM,
                LOG_EVENT_CONFIRMATION,
                &confirmation,
                (uint8_t)sizeof(confirmation)
            );
            experiment_telemetry_set_confirmation_result((uint32_t)confirmation);
        }
    }
}

int main(void)
{
    platform_init();
    uart_puts("EXP066 RESEARCH PLATFORM\nrp> ");
    for (;;) {
        platform_idle();
    }
}
