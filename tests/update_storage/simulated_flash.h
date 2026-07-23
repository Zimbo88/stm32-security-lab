#ifndef SIMULATED_FLASH_H
#define SIMULATED_FLASH_H

#include <stddef.h>
#include <stdint.h>

#include "boot_flash.h"
#include "stm32f429_memory_layout.h"

#define SIMULATED_FLASH_WRITE_LOG_CAPACITY 512U

typedef enum {
    SIMULATED_FLASH_WRITE_ERASE = 1,
    SIMULATED_FLASH_WRITE_PROGRAM = 2
} simulated_flash_write_kind_t;

typedef struct {
    simulated_flash_write_kind_t kind;
    uint32_t address;
    uint32_t sector_id;
    size_t length;
} simulated_flash_write_log_entry_t;

typedef struct {
    uint8_t storage[STM32F429_FLASH_TOTAL_SIZE];
    uint32_t operation_count;
    uint32_t erase_count;
    uint32_t program_count;
    uint32_t read_count;
    uint32_t last_erase_sector;
    uint32_t last_program_address;
    size_t last_program_length;
    uint32_t fail_after_operation;
    uint32_t fail_before_erase_sector;
    uint32_t fail_before_program_address;
    uint32_t fail_before_read_address;
    uint32_t corrupt_read_address;
    uint8_t corrupt_after_program;
    simulated_flash_write_log_entry_t write_log[SIMULATED_FLASH_WRITE_LOG_CAPACITY];
    size_t write_log_count;
    uint8_t write_log_overflow;
} simulated_flash_t;

void simulated_flash_init(simulated_flash_t *sim);
void simulated_flash_fail_after(simulated_flash_t *sim, uint32_t operation);
void simulated_flash_fail_before_erase_sector(simulated_flash_t *sim, uint32_t sector_id);
void simulated_flash_fail_before_program_address(simulated_flash_t *sim, uint32_t address);
void simulated_flash_fail_before_read_address(simulated_flash_t *sim, uint32_t address);
void simulated_flash_corrupt_read_address(simulated_flash_t *sim, uint32_t address);
void simulated_flash_corrupt_after_program(simulated_flash_t *sim, uint8_t enable);
void simulated_flash_sync_mapped(const simulated_flash_t *sim);
const boot_flash_ops_t *simulated_flash_ops(void);
uint8_t simulated_flash_peek(
    const simulated_flash_t *sim,
    uint32_t address,
    uint8_t *output,
    size_t length
);

#endif
