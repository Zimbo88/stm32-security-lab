#include "experiment_telemetry.h"

#include "boot_flash_target.h"
#include "boot_slot.h"
#include "platform.h"
#include "uart.h"

#define REG32(address) (*(volatile uint32_t *)(address))
#define VTOR_REG REG32(0xE000ED08UL)

static experiment_telemetry_report_t report
    __attribute__((section(".noinit"), used));

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, uint32_t length)
{
    crc = ~crc;
    for (uint32_t i = 0U; i < length; ++i) {
        crc ^= data[i];
        for (uint32_t bit = 0U; bit < 8U; ++bit) {
            const uint32_t mask = 0UL - (crc & 1UL);
            crc = (crc >> 1) ^ (0xEDB88320UL & mask);
        }
    }
    return ~crc;
}

static uint32_t report_crc_without_crc(
    const experiment_telemetry_report_t *input
)
{
    return crc32_update(
        0UL,
        (const uint8_t *)input,
        (uint32_t)((uintptr_t)&input->report_crc - (uintptr_t)input)
    );
}

uint8_t experiment_telemetry_report_valid(
    const experiment_telemetry_report_t *input
)
{
    if (input == 0) {
        return 0U;
    }

    return ((input->magic[0] == (uint8_t)EXPERIMENT_TELEMETRY_MAGIC0) &&
            (input->magic[1] == (uint8_t)EXPERIMENT_TELEMETRY_MAGIC1) &&
            (input->magic[2] == (uint8_t)EXPERIMENT_TELEMETRY_MAGIC2) &&
            (input->magic[3] == (uint8_t)EXPERIMENT_TELEMETRY_MAGIC3) &&
            (input->format_version == EXPERIMENT_TELEMETRY_FORMAT_VERSION) &&
            (input->report_crc == report_crc_without_crc(input)))
        ? 1U
        : 0U;
}

static uint32_t next_boot_counter(void)
{
    if (experiment_telemetry_report_valid(&report) == 0U) {
        return 1UL;
    }
    if (report.boot_counter == UINT32_MAX) {
        return UINT32_MAX;
    }
    return report.boot_counter + 1UL;
}

static uint8_t address_in_range(uint32_t address, uint32_t base, uint32_t end)
{
    return ((address >= base) && (address < end)) ? 1U : 0U;
}

static void capture_core_registers(void)
{
    __asm volatile("mrs %0,msp" : "=r"(report.msp));
    __asm volatile("mrs %0,psp" : "=r"(report.psp));
    __asm volatile("mrs %0,control" : "=r"(report.control));
    __asm volatile("mrs %0,primask" : "=r"(report.primask));
    __asm volatile("mrs %0,basepri" : "=r"(report.basepri));
    __asm volatile("mrs %0,faultmask" : "=r"(report.faultmask));

    report.vtor = VTOR_REG;
    if (report.vtor == PLATFORM_APP_BASE) {
        report.untrusted_observation_flags |=
            EXPERIMENT_TELEMETRY_OBS_VTOR_EXPECTED;
    }
    if (address_in_range(report.msp, PLATFORM_SRAM_BASE, PLATFORM_SRAM_END) != 0U) {
        report.untrusted_observation_flags |=
            EXPERIMENT_TELEMETRY_OBS_MSP_IN_RANGE;
    }
    if ((report.psp == 0UL) ||
        (address_in_range(report.psp, PLATFORM_SRAM_BASE, PLATFORM_SRAM_END) != 0U)) {
        report.untrusted_observation_flags |=
            EXPERIMENT_TELEMETRY_OBS_PSP_IN_RANGE;
    }
}

void experiment_telemetry_refresh_metadata(void)
{
    boot_flash_t flash;
    boot_metadata_record_t metadata;
    boot_metadata_recovery_t recovery;
    const boot_flash_status_t status =
        boot_flash_target_init_readonly(&flash);

    report.trusted_state_flags &=
        ~(EXPERIMENT_TELEMETRY_TRUSTED_METADATA_VALID |
          EXPERIMENT_TELEMETRY_TRUSTED_METADATA_AMBIGUOUS |
          EXPERIMENT_TELEMETRY_TRUSTED_METADATA_UNRECOVERABLE);
    report.selected_slot = BOOT_SLOT_NONE;
    report.confirmed_slot = BOOT_SLOT_NONE;
    report.candidate_slot = BOOT_SLOT_NONE;
    report.metadata_state = BOOT_METADATA_STATE_EMPTY;
    report.metadata_sequence = 0UL;
    report.remaining_trial_attempts = 0UL;
    report.image_version = 0UL;

    if (status != BOOT_FLASH_OK) {
        report.trusted_state_flags |=
            EXPERIMENT_TELEMETRY_TRUSTED_METADATA_UNRECOVERABLE;
        return;
    }

    const boot_metadata_status_t recovered =
        boot_metadata_recover_from_flash(&flash, &metadata, &recovery);
    if (recovered == BOOT_METADATA_ERR_AMBIGUOUS) {
        report.trusted_state_flags |=
            EXPERIMENT_TELEMETRY_TRUSTED_METADATA_AMBIGUOUS;
        return;
    }
    if (recovered != BOOT_METADATA_OK) {
        report.trusted_state_flags |=
            EXPERIMENT_TELEMETRY_TRUSTED_METADATA_UNRECOVERABLE;
        return;
    }

    report.trusted_state_flags |= EXPERIMENT_TELEMETRY_TRUSTED_METADATA_VALID;
    report.metadata_state = (uint32_t)metadata.state;
    report.metadata_sequence = metadata.sequence;
    report.confirmed_slot = metadata.active_slot;
    report.candidate_slot = metadata.candidate_slot;
    report.remaining_trial_attempts = metadata.boot_attempt_count;
    report.image_version = metadata.candidate_image_version;
    report.last_update_result = metadata.result;
    report.commit_marker = (uint32_t)recovery.selected_copy;

    if (metadata.state == BOOT_METADATA_STATE_PENDING_TRIAL) {
        report.selected_slot = metadata.candidate_slot;
    } else if (metadata.state == BOOT_METADATA_STATE_CONFIRMED) {
        report.selected_slot = metadata.active_slot;
    }
}

static void finalize_report(void)
{
    report.report_crc = report_crc_without_crc(&report);
}

void experiment_telemetry_init(uint32_t reset_cause_snapshot)
{
    const uint32_t boot_counter = next_boot_counter();

    report.magic[0] = (uint8_t)EXPERIMENT_TELEMETRY_MAGIC0;
    report.magic[1] = (uint8_t)EXPERIMENT_TELEMETRY_MAGIC1;
    report.magic[2] = (uint8_t)EXPERIMENT_TELEMETRY_MAGIC2;
    report.magic[3] = (uint8_t)EXPERIMENT_TELEMETRY_MAGIC3;
    report.format_version = EXPERIMENT_TELEMETRY_FORMAT_VERSION;
    report.boot_counter = boot_counter;
    report.reset_cause = reset_cause_snapshot;
    report.selected_slot = BOOT_SLOT_NONE;
    report.confirmed_slot = BOOT_SLOT_NONE;
    report.candidate_slot = BOOT_SLOT_NONE;
    report.metadata_state = BOOT_METADATA_STATE_EMPTY;
    report.metadata_sequence = 0UL;
    report.remaining_trial_attempts = 0UL;
    report.image_version = 0UL;
    report.vtor = 0UL;
    report.msp = 0UL;
    report.psp = 0UL;
    report.control = 0UL;
    report.primask = 0UL;
    report.basepri = 0UL;
    report.faultmask = 0UL;
    report.integrity_scan_status = EXPERIMENT_TELEMETRY_INTEGRITY_NOT_RUN;
    report.first_changed_region = EXPERIMENT_TELEMETRY_CHANGED_REGION_NONE;
    report.last_boot_policy_result = 0UL;
    report.last_update_result = 0UL;
    report.trusted_state_flags = 0UL;
    report.untrusted_observation_flags = 0UL;
    report.commit_marker = 0UL;
    report.report_crc = 0UL;

    capture_core_registers();
    experiment_telemetry_refresh_metadata();
    finalize_report();
}

void experiment_telemetry_set_boot_policy_result(uint32_t result)
{
    report.last_boot_policy_result = result;
    capture_core_registers();
    finalize_report();
}

void experiment_telemetry_set_confirmation_result(uint32_t result)
{
    experiment_telemetry_refresh_metadata();
    report.last_update_result = result;
    capture_core_registers();
    finalize_report();
}

const experiment_telemetry_report_t *experiment_telemetry_report(void)
{
    return &report;
}

static void print_u32_field(const char *name, uint32_t value)
{
    uart_puts(name);
    uart_putc('=');
    uart_put_hex32(value);
    uart_putc('\n');
}

void experiment_telemetry_print(void)
{
    print_u32_field("format_version", report.format_version);
    print_u32_field("boot_counter", report.boot_counter);
    print_u32_field("reset_cause", report.reset_cause);
    print_u32_field("selected_slot", report.selected_slot);
    print_u32_field("confirmed_slot", report.confirmed_slot);
    print_u32_field("candidate_slot", report.candidate_slot);
    print_u32_field("metadata_state", report.metadata_state);
    print_u32_field("metadata_sequence", report.metadata_sequence);
    print_u32_field("remaining_trial_attempts", report.remaining_trial_attempts);
    print_u32_field("image_version", report.image_version);
    print_u32_field("vtor", report.vtor);
    print_u32_field("msp", report.msp);
    print_u32_field("psp", report.psp);
    print_u32_field("control", report.control);
    print_u32_field("primask", report.primask);
    print_u32_field("basepri", report.basepri);
    print_u32_field("faultmask", report.faultmask);
    print_u32_field("integrity_scan_status", report.integrity_scan_status);
    print_u32_field("first_changed_region", report.first_changed_region);
    print_u32_field("last_boot_policy_result", report.last_boot_policy_result);
    print_u32_field("last_update_result", report.last_update_result);
    print_u32_field("trusted_state_flags", report.trusted_state_flags);
    print_u32_field("untrusted_observation_flags", report.untrusted_observation_flags);
    print_u32_field("commit_marker", report.commit_marker);
    print_u32_field("report_crc", report.report_crc);
}
