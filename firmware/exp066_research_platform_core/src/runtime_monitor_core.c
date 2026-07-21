#include "runtime_monitor_core.h"

#define RCC_CSR_BORRSTF (1UL << 25)
#define RCC_CSR_PINRSTF (1UL << 26)
#define RCC_CSR_PORRSTF (1UL << 27)
#define RCC_CSR_SFTRSTF (1UL << 28)
#define RCC_CSR_IWDGRSTF (1UL << 29)
#define RCC_CSR_WWDGRSTF (1UL << 30)
#define RCC_CSR_LPWRRSTF (1UL << 31)

static void store_le32(uint8_t bytes[4], uint32_t value)
{
    bytes[0] = (uint8_t)(value & 0xFFU);
    bytes[1] = (uint8_t)((value >> 8U) & 0xFFU);
    bytes[2] = (uint8_t)((value >> 16U) & 0xFFU);
    bytes[3] = (uint8_t)((value >> 24U) & 0xFFU);
}

uint8_t rsm_information_allowed(
    rsm_information_class_t information_class,
    uint8_t restricted_enabled
)
{
    if (information_class == RSM_INFO_PUBLIC) {
        return 1U;
    }
    if (information_class == RSM_INFO_RESTRICTED) {
        return restricted_enabled != 0U ? 1U : 0U;
    }
    return 0U;
}

uint32_t rsm_event_domain(rsm_event_id_t event_id)
{
    return (uint32_t)event_id & 0xFF00UL;
}

uint32_t rsm_crc32_update(
    uint32_t crc,
    const uint8_t *data,
    uint32_t length
)
{
    crc = ~crc;
    for (uint32_t i = 0U; i < length; ++i) {
        crc ^= data[i];
        for (uint32_t bit = 0U; bit < 8U; ++bit) {
            const uint32_t mask = 0UL - (crc & 1UL);
            crc = (crc >> 1U) ^ (0xEDB88320UL & mask);
        }
    }

    return ~crc;
}

uint32_t rsm_uid_fingerprint_words(
    uint32_t uid0,
    uint32_t uid1,
    uint32_t uid2
)
{
    uint8_t uid_bytes[12];

    store_le32(&uid_bytes[0], uid0);
    store_le32(&uid_bytes[4], uid1);
    store_le32(&uid_bytes[8], uid2);
    return rsm_crc32_update(0UL, uid_bytes, (uint32_t)sizeof(uid_bytes));
}

const char *rsm_reset_cause_name(uint32_t reset_csr)
{
    if ((reset_csr & RCC_CSR_IWDGRSTF) != 0UL) {
        return "iwdg";
    }
    if ((reset_csr & RCC_CSR_WWDGRSTF) != 0UL) {
        return "wwdg";
    }
    if ((reset_csr & RCC_CSR_SFTRSTF) != 0UL) {
        return "software";
    }
    if ((reset_csr & RCC_CSR_PORRSTF) != 0UL) {
        return "power_on";
    }
    if ((reset_csr & RCC_CSR_BORRSTF) != 0UL) {
        return "brownout";
    }
    if ((reset_csr & RCC_CSR_PINRSTF) != 0UL) {
        return "pin";
    }
    if ((reset_csr & RCC_CSR_LPWRRSTF) != 0UL) {
        return "low_power";
    }
    return "unknown";
}

const char *rsm_slot_name(uint32_t slot)
{
    if (slot == 0UL) {
        return "A";
    }
    if (slot == 1UL) {
        return "B";
    }
    return "unavailable";
}

const char *rsm_bool_pass_fail(uint8_t passed)
{
    return passed != 0U ? "pass" : "fail";
}

const char *rsm_status_name(rsm_status_t status)
{
    switch (status) {
    case RSM_STATUS_OK:
        return "ok";
    case RSM_STATUS_DEGRADED:
        return "degraded";
    case RSM_STATUS_FAILED:
        return "failed";
    case RSM_STATUS_UNAVAILABLE:
        return "unavailable";
    case RSM_STATUS_BUSY:
        return "busy";
    case RSM_STATUS_INVALID_ARGUMENT:
    default:
        return "invalid_argument";
    }
}

uint32_t rsm_saturating_increment(uint32_t value, uint8_t *saturated)
{
    if (value >= (UINT32_MAX - 1UL)) {
        if (saturated != 0) {
            *saturated = 1U;
        }
        return UINT32_MAX;
    }
    if (saturated != 0) {
        *saturated = 0U;
    }
    return value + 1UL;
}

const char *rsm_clock_source_name_from_cfgr(uint32_t cfgr)
{
    switch ((cfgr >> 2U) & 0x3UL) {
    case 0UL:
        return "hsi";
    case 1UL:
        return "hse";
    case 2UL:
        return "pll";
    default:
        return "unavailable";
    }
}

static uint32_t hpre_divisor(uint32_t cfgr)
{
    switch ((cfgr >> 4U) & 0x0FUL) {
    case 0x8UL:
        return 2UL;
    case 0x9UL:
        return 4UL;
    case 0xAUL:
        return 8UL;
    case 0xBUL:
        return 16UL;
    case 0xCUL:
        return 64UL;
    case 0xDUL:
        return 128UL;
    case 0xEUL:
        return 256UL;
    case 0xFUL:
        return 512UL;
    default:
        return 1UL;
    }
}

uint8_t rsm_hclk_hz_from_cfgr(uint32_t cfgr, uint32_t *hclk_hz)
{
    if (hclk_hz == 0) {
        return 0U;
    }
    if (((cfgr >> 2U) & 0x3UL) != 0UL) {
        *hclk_hz = 0UL;
        return 0U;
    }
    *hclk_hz = 16000000UL / hpre_divisor(cfgr);
    return 1U;
}

static void out_putc(const rsm_output_t *output, char value)
{
    output->putc(output->context, value);
}

static void out_puts(const rsm_output_t *output, const char *text)
{
    output->puts(output->context, text);
}

static void out_u32(const rsm_output_t *output, uint32_t value)
{
    char buffer[10];
    uint32_t length = 0U;

    if (value == 0UL) {
        out_putc(output, '0');
        return;
    }

    while (value != 0UL) {
        buffer[length] = (char)('0' + (value % 10UL));
        ++length;
        value /= 10UL;
    }
    while (length != 0U) {
        --length;
        out_putc(output, buffer[length]);
    }
}

static void out_hex32(const rsm_output_t *output, uint32_t value, uint8_t prefix)
{
    static const char hex[] = "0123456789ABCDEF";

    if (prefix != 0U) {
        out_puts(output, "0x");
    }
    for (uint32_t shift = 28U;; shift -= 4U) {
        out_putc(output, hex[(value >> shift) & 0x0FUL]);
        if (shift == 0U) {
            break;
        }
    }
}

static void kv_str(const rsm_output_t *output, const char *key, const char *value)
{
    out_puts(output, key);
    out_putc(output, '=');
    out_puts(output, value);
    out_putc(output, '\n');
}

static void kv_u32(const rsm_output_t *output, const char *key, uint32_t value)
{
    out_puts(output, key);
    out_putc(output, '=');
    out_u32(output, value);
    out_putc(output, '\n');
}

static void kv_hex32(
    const rsm_output_t *output,
    const char *key,
    uint32_t value,
    uint8_t prefix
)
{
    out_puts(output, key);
    out_putc(output, '=');
    out_hex32(output, value, prefix);
    out_putc(output, '\n');
}

void rsm_format_public_status(
    const rsm_public_status_output_t *status,
    const rsm_output_t *output
)
{
    if ((status == 0) || (output == 0) ||
        (output->putc == 0) || (output->puts == 0)) {
        return;
    }

    kv_u32(output, "rsm.schema", status->schema_version);
    kv_str(output, "rsm.status", status->status);
    kv_str(output, "rsm.build.diagnostic_mode", status->build_diagnostic_mode);
    kv_str(output, "rsm.device.family", status->device_family);
    kv_hex32(output, "rsm.device.id", status->device_id, 1U);
    kv_hex32(output, "rsm.device.revision", status->revision_id, 1U);
    kv_u32(output, "rsm.device.flash_kib", status->flash_kib);
    kv_hex32(
        output,
        "rsm.device.uid_fingerprint",
        status->uid_fingerprint,
        0U
    );
    kv_str(output, "rsm.cpu.core", status->cpu_core);
    kv_str(output, "rsm.cpu.fpu", status->cpu_fpu);
    kv_str(output, "rsm.cpu.mpu", status->cpu_mpu);
    kv_str(output, "rsm.cpu.clock_source", status->cpu_clock_source);
    if (status->hclk_available != 0U) {
        kv_u32(output, "rsm.cpu.hclk_hz", status->hclk_hz);
    } else {
        kv_str(output, "rsm.cpu.hclk_hz", "unavailable");
    }
    kv_hex32(output, "rsm.cpu.cpuid", status->cpuid, 1U);
    kv_u32(output, "rsm.boot.sequence", status->boot_sequence);
    kv_str(output, "rsm.boot.slot", status->boot_slot);
    if (status->firmware_version_available != 0U) {
        kv_u32(output, "rsm.boot.firmware_version", status->firmware_version);
    } else {
        kv_str(output, "rsm.boot.firmware_version", "unavailable");
    }
    kv_str(output, "rsm.boot.last_reset", status->last_reset);
    kv_str(output, "rsm.boot.self_tests", status->boot_self_tests);
    kv_str(output, "rsm.security.flash", status->security_flash);
    kv_str(output, "rsm.security.vectors", status->security_vectors);
    kv_str(output, "rsm.security.stack", status->security_stack);
    kv_str(
        output,
        "rsm.security.option_policy",
        status->security_option_policy
    );
    kv_str(output, "rsm.health.state", status->health_state);
    kv_u32(
        output,
        "rsm.evidence.last_event_sequence",
        status->last_event_sequence
    );
    kv_str(output, "rsm.evidence.last_fault", status->last_fault);
    kv_str(output, "rsm.evidence.timebase", status->timebase);
    kv_str(output, "rsm.evidence.boot_context", status->boot_context);
    kv_u32(output, "rsm.events.count", status->event_count);
    kv_u32(output, "rsm.events.dropped", status->event_dropped);
    kv_u32(
        output,
        "rsm.counter.diagnostic_denials",
        status->diagnostic_denials
    );
    kv_u32(
        output,
        "rsm.counter.event_log_overflows",
        status->event_log_overflows
    );
    kv_str(output, "rsm.policy.restricted", status->restricted_policy);
    kv_str(output, "rsm.policy.secret", status->secret_policy);
}
