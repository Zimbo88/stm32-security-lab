#include "boot_flash_target.h"

#include <stdint.h>
#include <string.h>

#define REG32(address) (*(volatile uint32_t *)(address))

#define FLASH_REGISTER_BASE 0x40023C00UL
#define FLASH_ACR_REG      REG32(FLASH_REGISTER_BASE + 0x00UL)
#define FLASH_KEYR_REG     REG32(FLASH_REGISTER_BASE + 0x04UL)
#define FLASH_SR_REG       REG32(FLASH_REGISTER_BASE + 0x0CUL)
#define FLASH_CR_REG       REG32(FLASH_REGISTER_BASE + 0x10UL)

#define FLASH_KEY1 0x45670123UL
#define FLASH_KEY2 0xCDEF89ABUL

#define FLASH_ACR_DCEN  (1UL << 10)
#define FLASH_ACR_DCRST (1UL << 12)

#define FLASH_SR_EOP    (1UL << 0)
#define FLASH_SR_OPERR  (1UL << 1)
#define FLASH_SR_WRPERR (1UL << 4)
#define FLASH_SR_PGAERR (1UL << 5)
#define FLASH_SR_PGPERR (1UL << 6)
#define FLASH_SR_PGSERR (1UL << 7)
#define FLASH_SR_RDERR  (1UL << 8)
#define FLASH_SR_BSY    (1UL << 16)
#define FLASH_SR_CLEAR_MASK \
    (FLASH_SR_EOP | FLASH_SR_OPERR | FLASH_SR_WRPERR | FLASH_SR_PGAERR | \
     FLASH_SR_PGPERR | FLASH_SR_PGSERR | FLASH_SR_RDERR)
#define FLASH_SR_ERROR_MASK \
    (FLASH_SR_OPERR | FLASH_SR_WRPERR | FLASH_SR_PGAERR | FLASH_SR_PGPERR | \
     FLASH_SR_PGSERR | FLASH_SR_RDERR)

#define FLASH_CR_PG       (1UL << 0)
#define FLASH_CR_SER      (1UL << 1)
#define FLASH_CR_MER      (1UL << 2)
#define FLASH_CR_SNB_SHIFT 3U
#define FLASH_CR_SNB_MASK (0x1FUL << FLASH_CR_SNB_SHIFT)
#define FLASH_CR_PSIZE_MASK (3UL << 8)
#define FLASH_CR_STRT     (1UL << 16)
#define FLASH_CR_LOCK     (1UL << 31)

#define RAMFUNC __attribute__((section(".ramfunc"), noinline))

#ifndef BOOT_FLASH_TARGET_HOST_TEST
extern uint8_t _sramfunc;
extern uint8_t _eramfunc;
#endif

static const boot_flash_region_t metadata_write_regions[] = {
    {STM32F429_BOOT_METADATA_A_BASE, STM32F429_BOOT_METADATA_A_END},
    {STM32F429_BOOT_METADATA_B_BASE, STM32F429_BOOT_METADATA_B_END},
};

static uint8_t target_range_is_flash(uint32_t address, size_t length)
{
    if ((address < STM32F429_FLASH_BASE) ||
        (address > STM32F429_FLASH_END) ||
        (length > STM32F429_FLASH_TOTAL_SIZE)) {
        return 0U;
    }

    const uint32_t max_length = STM32F429_FLASH_END - address;
    return (length <= (size_t)max_length) ? 1U : 0U;
}

#if defined(BOOT_ENABLE_LAB_CANARY_WRITES) && BOOT_ENABLE_LAB_CANARY_WRITES
static uint8_t target_region_is_protected(
    uint32_t start,
    uint32_t end
)
{
    const boot_flash_region_t protected_regions[] = {
        {STM32F429_BOOTLOADER_BASE, STM32F429_BOOTLOADER_END},
        {STM32F429_BOOT_METADATA_A_BASE, STM32F429_BOOT_METADATA_A_END},
        {STM32F429_BOOT_METADATA_B_BASE, STM32F429_BOOT_METADATA_B_END},
        {STM32F429_UPDATE_METADATA_BASE, STM32F429_UPDATE_METADATA_END},
        {STM32F429_RECOVERY_BASE, STM32F429_RECOVERY_END},
    };

    if ((start >= end) ||
        (start < STM32F429_FLASH_BASE) ||
        (end > STM32F429_FLASH_END)) {
        return 1U;
    }

    for (size_t i = 0U;
         i < (sizeof(protected_regions) / sizeof(protected_regions[0]));
         ++i) {
        if ((start < protected_regions[i].end) &&
            (protected_regions[i].base < end)) {
            return 1U;
        }
    }

    return 0U;
}

static uint8_t lab_regions_are_valid(
    const boot_flash_region_t *regions,
    size_t region_count
)
{
    if ((regions == NULL) || (region_count == 0U)) {
        return 0U;
    }

    for (size_t i = 0U; i < region_count; ++i) {
        if (target_region_is_protected(regions[i].base, regions[i].end) != 0U) {
            return 0U;
        }
        for (size_t j = i + 1U; j < region_count; ++j) {
            if ((regions[i].base < regions[j].end) &&
                (regions[j].base < regions[i].end)) {
                return 0U;
            }
        }
    }

    return 1U;
}
#endif

static inline void compiler_barrier(void)
{
    __asm volatile ("" ::: "memory");
}

#ifndef BOOT_FLASH_TARGET_HOST_TEST
static uint32_t interrupt_save_disable(void)
{
    uint32_t primask = 0U;
    __asm volatile (
        "mrs %0, primask\n"
        "cpsid i\n"
        : "=r" (primask)
        :
        : "memory"
    );
    return primask;
}

static void interrupt_restore(uint32_t primask)
{
    if ((primask & 1UL) == 0UL) {
        __asm volatile ("cpsie i" ::: "memory");
    }
}
#else
static uint32_t interrupt_save_disable(boot_flash_target_host_context_t *host)
{
    if (host != NULL) {
        host->saved_primask = 0U;
        host->critical_enter_count += 1UL;
    }
    return 0U;
}

static void interrupt_restore(
    boot_flash_target_host_context_t *host,
    uint32_t primask
)
{
    if (host != NULL) {
        host->restored_primask = primask;
        host->critical_exit_count += 1UL;
    }
}
#endif

#ifndef BOOT_FLASH_TARGET_HOST_TEST
static uint32_t flash_acr_read(void) { return FLASH_ACR_REG; }
static void flash_acr_write(uint32_t value) { FLASH_ACR_REG = value; }
static void flash_sr_write(uint32_t value) { FLASH_SR_REG = value; }
static uint32_t flash_cr_read(void) { return FLASH_CR_REG; }
static void flash_cr_write(uint32_t value) { FLASH_CR_REG = value; }
static void flash_keyr_write(uint32_t value) { FLASH_KEYR_REG = value; }
static inline uint8_t flash_read_u8(uint32_t address)
{
    return *(const volatile uint8_t *)(uintptr_t)address;
}
static inline void flash_write_u8(uint32_t address, uint8_t value)
{
    *(volatile uint8_t *)(uintptr_t)address = value;
}
#else
static boot_flash_target_host_context_t *host_context(void *context)
{
    return (boot_flash_target_host_context_t *)context;
}

static void flash_sr_write_host(
    boot_flash_target_host_context_t *host,
    uint32_t value
)
{
    host->sr &= ~value;
}

static uint32_t flash_cr_read_host(boot_flash_target_host_context_t *host)
{
    return host->cr;
}

static void flash_cr_write_host(
    boot_flash_target_host_context_t *host,
    uint32_t value
)
{
    host->cr = value;
}

static void flash_keyr_write_host(
    boot_flash_target_host_context_t *host,
    uint32_t value
)
{
    host->keyr = value;
    if (value == FLASH_KEY2) {
        host->cr &= ~FLASH_CR_LOCK;
    }
}

static uint8_t flash_read_u8_host(
    boot_flash_target_host_context_t *host,
    uint32_t address
)
{
    return host->storage[address - STM32F429_FLASH_BASE];
}

static void flash_write_u8_host(
    boot_flash_target_host_context_t *host,
    uint32_t address,
    uint8_t value
)
{
    const size_t offset = (size_t)(address - STM32F429_FLASH_BASE);
    host->storage[offset] = (uint8_t)(host->storage[offset] & value);
}
#endif

#ifndef BOOT_FLASH_TARGET_HOST_TEST
static uint8_t RAMFUNC flash_wait_not_busy_raw(void)
{
    uint32_t timeout = BOOT_FLASH_TARGET_MAX_BUSY_POLLS;

    while (((FLASH_SR_REG & FLASH_SR_BSY) != 0UL) && (timeout != 0UL)) {
        compiler_barrier();
        --timeout;
    }

    if ((FLASH_SR_REG & FLASH_SR_BSY) != 0UL) {
        return 0U;
    }

    return ((FLASH_SR_REG & FLASH_SR_ERROR_MASK) == 0UL) ? 1U : 0U;
}

static uint8_t RAMFUNC flash_erase_sector_critical(uint32_t sector_id)
{
    uint32_t cr = FLASH_CR_REG;
    cr &= ~(FLASH_CR_PG | FLASH_CR_SER | FLASH_CR_MER |
            FLASH_CR_SNB_MASK | FLASH_CR_PSIZE_MASK);
    cr |= FLASH_CR_SER | (sector_id << FLASH_CR_SNB_SHIFT);
    FLASH_CR_REG = cr;
    compiler_barrier();
    FLASH_CR_REG = cr | FLASH_CR_STRT;

    const uint8_t ok = flash_wait_not_busy_raw();
    FLASH_CR_REG &= ~FLASH_CR_SER;
    return ok;
}

static uint8_t RAMFUNC flash_program_critical(
    uint32_t address,
    const uint8_t *data,
    size_t length
)
{
    uint32_t cr = FLASH_CR_REG;
    cr &= ~(FLASH_CR_SER | FLASH_CR_MER | FLASH_CR_SNB_MASK |
            FLASH_CR_PSIZE_MASK);
    cr |= FLASH_CR_PG;
    FLASH_CR_REG = cr;

    for (size_t i = 0U; i < length; ++i) {
        flash_write_u8(address + (uint32_t)i, data[i]);
        if (flash_wait_not_busy_raw() == 0U) {
            FLASH_CR_REG &= ~FLASH_CR_PG;
            return 0U;
        }
    }

    FLASH_CR_REG &= ~FLASH_CR_PG;
    return 1U;
}
#else
static uint8_t flash_wait_not_busy_host(boot_flash_target_host_context_t *host)
{
    if (host->busy_timeout != 0U) {
        host->sr |= FLASH_SR_BSY;
        return 0U;
    }

    host->sr &= ~FLASH_SR_BSY;
    return ((host->sr & FLASH_SR_ERROR_MASK) == 0UL) ? 1U : 0U;
}

static uint8_t flash_erase_sector_critical_host(
    boot_flash_target_host_context_t *host,
    uint32_t sector_id
)
{
    boot_flash_sector_t sector;

    if (boot_flash_sector_by_id(sector_id, &sector) != BOOT_FLASH_OK) {
        host->sr |= FLASH_SR_OPERR;
        return 0U;
    }

    if (host->erase_error != 0U) {
        host->sr |= FLASH_SR_OPERR;
        return 0U;
    }

    if (flash_wait_not_busy_host(host) == 0U) {
        return 0U;
    }

    memset(
        &host->storage[sector.base - STM32F429_FLASH_BASE],
        0xFF,
        sector.size
    );
    return 1U;
}

static uint8_t flash_program_critical_host(
    boot_flash_target_host_context_t *host,
    uint32_t address,
    const uint8_t *data,
    size_t length
)
{
    if (host->program_error != 0U) {
        host->sr |= FLASH_SR_PGSERR;
        return 0U;
    }

    if (flash_wait_not_busy_host(host) == 0U) {
        return 0U;
    }

    for (size_t i = 0U; i < length; ++i) {
        const uint32_t current_address = address + (uint32_t)i;
        const uint8_t current = flash_read_u8_host(host, current_address);
        if ((uint8_t)(current & data[i]) != data[i]) {
            host->sr |= FLASH_SR_PGAERR;
            return 0U;
        }
        flash_write_u8_host(host, current_address, data[i]);
        if (host->corrupt_after_program != 0U && i == 0U) {
            host->storage[current_address - STM32F429_FLASH_BASE] ^= 0x01U;
        }
    }

    return 1U;
}
#endif

static void flash_data_cache_prepare(uint32_t *saved_acr)
{
    if (saved_acr == NULL) {
        return;
    }

#ifndef BOOT_FLASH_TARGET_HOST_TEST
    *saved_acr = flash_acr_read();
    if ((*saved_acr & FLASH_ACR_DCEN) != 0UL) {
        flash_acr_write(*saved_acr & ~FLASH_ACR_DCEN);
    }
#else
    (void)saved_acr;
#endif
    compiler_barrier();
}

static void flash_data_cache_restore(uint32_t saved_acr)
{
#ifndef BOOT_FLASH_TARGET_HOST_TEST
    if ((saved_acr & FLASH_ACR_DCEN) != 0UL) {
        flash_acr_write(flash_acr_read() | FLASH_ACR_DCRST);
        compiler_barrier();
        flash_acr_write(flash_acr_read() & ~FLASH_ACR_DCRST);
        flash_acr_write(flash_acr_read() | FLASH_ACR_DCEN);
    }
#else
    (void)saved_acr;
#endif
    compiler_barrier();
}

static boot_flash_status_t target_disabled_read(
    void *context,
    uint32_t address,
    uint8_t *output,
    size_t length
)
{
    (void)context;
    (void)address;
    (void)output;
    (void)length;
    return BOOT_FLASH_ERR_BACKEND;
}

static boot_flash_status_t target_read(
    void *context,
    uint32_t address,
    uint8_t *output,
    size_t length
)
{
    if (((length != 0U) && (output == NULL)) ||
        (target_range_is_flash(address, length) == 0U)) {
        return BOOT_FLASH_ERR_BACKEND;
    }

    if (length == 0U) {
        return BOOT_FLASH_OK;
    }

#ifndef BOOT_FLASH_TARGET_HOST_TEST
    (void)context;
    memcpy(output, (const void *)(uintptr_t)address, length);
#else
    boot_flash_target_host_context_t *host = host_context(context);
    if (host == NULL) {
        return BOOT_FLASH_ERR_BACKEND;
    }
    memcpy(output, &host->storage[address - STM32F429_FLASH_BASE], length);
#endif
    return BOOT_FLASH_OK;
}

static boot_flash_status_t target_disabled_erase_sector(
    void *context,
    uint32_t sector_id
)
{
    (void)context;
    (void)sector_id;
    return BOOT_FLASH_ERR_PROTECTED_REGION;
}

static boot_flash_status_t target_disabled_program(
    void *context,
    uint32_t address,
    const uint8_t *data,
    size_t length
)
{
    (void)context;
    (void)address;
    (void)data;
    (void)length;
    return BOOT_FLASH_ERR_PROTECTED_REGION;
}

static boot_flash_status_t target_unlock(void *context)
{
#ifndef BOOT_FLASH_TARGET_HOST_TEST
    (void)context;
    if ((flash_cr_read() & FLASH_CR_LOCK) == 0UL) {
        return BOOT_FLASH_OK;
    }
    flash_keyr_write(FLASH_KEY1);
    flash_keyr_write(FLASH_KEY2);
    return ((flash_cr_read() & FLASH_CR_LOCK) == 0UL)
        ? BOOT_FLASH_OK
        : BOOT_FLASH_ERR_BACKEND;
#else
    boot_flash_target_host_context_t *host = host_context(context);
    if (host == NULL || host->unlock_failure != 0U) {
        return BOOT_FLASH_ERR_BACKEND;
    }
    if ((flash_cr_read_host(host) & FLASH_CR_LOCK) == 0UL) {
        return BOOT_FLASH_OK;
    }
    flash_keyr_write_host(host, FLASH_KEY1);
    flash_keyr_write_host(host, FLASH_KEY2);
    return ((flash_cr_read_host(host) & FLASH_CR_LOCK) == 0UL)
        ? BOOT_FLASH_OK
        : BOOT_FLASH_ERR_BACKEND;
#endif
}

static boot_flash_status_t target_lock(void *context)
{
#ifndef BOOT_FLASH_TARGET_HOST_TEST
    (void)context;
    flash_cr_write(flash_cr_read() | FLASH_CR_LOCK);
    return ((flash_cr_read() & FLASH_CR_LOCK) != 0UL)
        ? BOOT_FLASH_OK
        : BOOT_FLASH_ERR_BACKEND;
#else
    boot_flash_target_host_context_t *host = host_context(context);
    if (host == NULL) {
        return BOOT_FLASH_ERR_BACKEND;
    }
    if (host->lock_failure == 0U) {
        flash_cr_write_host(host, flash_cr_read_host(host) | FLASH_CR_LOCK);
    }
    return ((flash_cr_read_host(host) & FLASH_CR_LOCK) != 0UL)
        ? BOOT_FLASH_OK
        : BOOT_FLASH_ERR_BACKEND;
#endif
}

static void target_clear_status_flags(void *context)
{
#ifndef BOOT_FLASH_TARGET_HOST_TEST
    (void)context;
    flash_sr_write(FLASH_SR_CLEAR_MASK);
#else
    boot_flash_target_host_context_t *host = host_context(context);
    if (host != NULL) {
        flash_sr_write_host(host, FLASH_SR_CLEAR_MASK);
    }
#endif
}

static boot_flash_status_t target_erase_sector(void *context, uint32_t sector_id)
{
    boot_flash_status_t status = target_unlock(context);
    boot_flash_status_t lock_status = BOOT_FLASH_OK;
    uint32_t saved_acr = 0U;
    uint32_t primask = 0U;
    uint8_t ok = 0U;

    if (status != BOOT_FLASH_OK) {
        return status;
    }

    target_clear_status_flags(context);
    flash_data_cache_prepare(&saved_acr);

#ifndef BOOT_FLASH_TARGET_HOST_TEST
    primask = interrupt_save_disable();
    ok = flash_erase_sector_critical(sector_id);
    interrupt_restore(primask);
#else
    boot_flash_target_host_context_t *host = host_context(context);
    if (host == NULL) {
        ok = 0U;
    } else {
        primask = interrupt_save_disable(host);
        ok = flash_erase_sector_critical_host(host, sector_id);
        interrupt_restore(host, primask);
    }
#endif

    flash_data_cache_restore(saved_acr);
    lock_status = target_lock(context);

    if (ok == 0U) {
        return BOOT_FLASH_ERR_BACKEND;
    }
    return (lock_status == BOOT_FLASH_OK) ? BOOT_FLASH_OK : lock_status;
}

static boot_flash_status_t target_program(
    void *context,
    uint32_t address,
    const uint8_t *data,
    size_t length
)
{
    boot_flash_status_t status;
    boot_flash_status_t lock_status;
    uint32_t saved_acr = 0U;
    uint32_t primask = 0U;
    uint8_t ok = 0U;

    if ((data == NULL) || (length == 0U) ||
        (target_range_is_flash(address, length) == 0U)) {
        return BOOT_FLASH_ERR_BACKEND;
    }

    status = target_unlock(context);
    if (status != BOOT_FLASH_OK) {
        return status;
    }

    target_clear_status_flags(context);
    flash_data_cache_prepare(&saved_acr);

#ifndef BOOT_FLASH_TARGET_HOST_TEST
    primask = interrupt_save_disable();
    ok = flash_program_critical(address, data, length);
    interrupt_restore(primask);
#else
    boot_flash_target_host_context_t *host = host_context(context);
    if (host == NULL) {
        ok = 0U;
    } else {
        primask = interrupt_save_disable(host);
        ok = flash_program_critical_host(host, address, data, length);
        interrupt_restore(host, primask);
    }
#endif

    flash_data_cache_restore(saved_acr);
    lock_status = target_lock(context);

    if (ok == 0U) {
        return BOOT_FLASH_ERR_BACKEND;
    }
    return (lock_status == BOOT_FLASH_OK) ? BOOT_FLASH_OK : lock_status;
}

static const boot_flash_ops_t disabled_ops = {
    .read = target_disabled_read,
    .erase_sector = target_disabled_erase_sector,
    .program = target_disabled_program,
};

static const boot_flash_ops_t readonly_ops = {
    .read = target_read,
    .erase_sector = target_disabled_erase_sector,
    .program = target_disabled_program,
};

static const boot_flash_ops_t writable_ops = {
    .read = target_read,
    .erase_sector = target_erase_sector,
    .program = target_program,
};

uint8_t boot_flash_target_ramfunc_bounds_valid(void)
{
#ifndef BOOT_FLASH_TARGET_HOST_TEST
    const uintptr_t erase_address = (uintptr_t)&flash_erase_sector_critical;
    const uintptr_t program_address = (uintptr_t)&flash_program_critical;
    const uintptr_t wait_address = (uintptr_t)&flash_wait_not_busy_raw;
    const uintptr_t start = (uintptr_t)&_sramfunc;
    const uintptr_t end = (uintptr_t)&_eramfunc;

    return ((start < end) &&
            (erase_address >= start) &&
            (erase_address < end) &&
            (program_address >= start) &&
            (program_address < end) &&
            (wait_address >= start) &&
            (wait_address < end))
        ? 1U
        : 0U;
#else
    return 1U;
#endif
}

boot_flash_status_t boot_flash_target_init_disabled(boot_flash_t *flash)
{
    return boot_flash_init(
        flash,
        NULL,
        &disabled_ops,
        NULL,
        0U,
        BOOT_FLASH_TARGET_PROGRAM_ALIGNMENT
    );
}

boot_flash_status_t boot_flash_target_init_readonly(boot_flash_t *flash)
{
    return boot_flash_init(
        flash,
        NULL,
        &readonly_ops,
        NULL,
        0U,
        BOOT_FLASH_TARGET_PROGRAM_ALIGNMENT
    );
}

boot_flash_status_t boot_flash_target_init(boot_flash_t *flash)
{
    if (boot_flash_target_ramfunc_bounds_valid() == 0U) {
        return BOOT_FLASH_ERR_BACKEND;
    }

    return boot_flash_init(
        flash,
        NULL,
        &writable_ops,
        NULL,
        0U,
        BOOT_FLASH_TARGET_PROGRAM_ALIGNMENT
    );
}

boot_flash_status_t boot_flash_target_init_metadata(boot_flash_t *flash)
{
    if (boot_flash_target_ramfunc_bounds_valid() == 0U) {
        return BOOT_FLASH_ERR_BACKEND;
    }

    return boot_flash_init(
        flash,
        NULL,
        &writable_ops,
        metadata_write_regions,
        sizeof(metadata_write_regions) / sizeof(metadata_write_regions[0]),
        BOOT_FLASH_TARGET_PROGRAM_ALIGNMENT
    );
}

boot_flash_status_t boot_flash_target_init_lab_canary(
    boot_flash_t *flash,
    const boot_flash_region_t *regions,
    size_t region_count
)
{
#if defined(BOOT_ENABLE_LAB_CANARY_WRITES) && BOOT_ENABLE_LAB_CANARY_WRITES
    if ((boot_flash_target_ramfunc_bounds_valid() == 0U) ||
        (lab_regions_are_valid(regions, region_count) == 0U)) {
        return BOOT_FLASH_ERR_PROTECTED_REGION;
    }

    return boot_flash_init(
        flash,
        NULL,
        &writable_ops,
        regions,
        region_count,
        BOOT_FLASH_TARGET_PROGRAM_ALIGNMENT
    );
#else
    (void)flash;
    (void)regions;
    (void)region_count;
    return BOOT_FLASH_ERR_PROTECTED_REGION;
#endif
}

#ifdef BOOT_FLASH_TARGET_HOST_TEST
void boot_flash_target_host_context_init(boot_flash_target_host_context_t *context)
{
    if (context == NULL) {
        return;
    }

    memset(context->storage, 0xFF, sizeof(context->storage));
    context->acr = 0U;
    context->sr = 0U;
    context->cr = FLASH_CR_LOCK;
    context->keyr = 0U;
    context->unlock_failure = 0U;
    context->busy_timeout = 0U;
    context->erase_error = 0U;
    context->program_error = 0U;
    context->lock_failure = 0U;
    context->corrupt_after_program = 0U;
    context->saved_primask = UINT32_MAX;
    context->restored_primask = UINT32_MAX;
    context->critical_enter_count = 0U;
    context->critical_exit_count = 0U;
    context->ramfunc_valid = 1U;
}

boot_flash_status_t boot_flash_target_init_host(
    boot_flash_t *flash,
    boot_flash_target_host_context_t *context,
    const boot_flash_region_t *write_regions,
    size_t write_region_count
)
{
    if ((context == NULL) || (context->ramfunc_valid == 0U)) {
        return BOOT_FLASH_ERR_BACKEND;
    }

    return boot_flash_init(
        flash,
        context,
        &writable_ops,
        write_regions,
        write_region_count,
        BOOT_FLASH_TARGET_PROGRAM_ALIGNMENT
    );
}
#endif
