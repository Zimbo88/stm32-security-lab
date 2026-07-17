#ifndef BOARD_H
#define BOARD_H

#include <stdint.h>

#include "stm32f429_memory_layout.h"

/*---------------------------------------------------------------------------
 * Board identification
 *---------------------------------------------------------------------------*/

#define BOARD_NAME              "STM32F429IGT6 Lab Board"

/*---------------------------------------------------------------------------
 * Clock sources
 *---------------------------------------------------------------------------*/

#define BOARD_HSI_HZ            16000000UL
#define BOARD_HSE_HZ            25000000UL
#define BOARD_LSE_HZ            32768UL

#define BOARD_TARGET_SYSCLK_HZ  180000000UL

/*---------------------------------------------------------------------------
 * Internal memories
 *---------------------------------------------------------------------------*/

#define BOARD_FLASH_BASE        STM32F429_FLASH_BASE
#define BOARD_FLASH_SIZE        STM32F429_FLASH_TOTAL_SIZE

#define BOARD_FLASH_BANK1_BASE  STM32F429_FLASH_BANK1_BASE
#define BOARD_FLASH_BANK2_BASE  STM32F429_FLASH_BANK1_END
#define BOARD_FLASH_END         STM32F429_FLASH_END

#define BOARD_SRAM_BASE         STM32F429_MAIN_SRAM_BASE
#define BOARD_SRAM_SIZE         STM32F429_MAIN_SRAM_SUPPORTED_SIZE
#define BOARD_SRAM_END          STM32F429_MAIN_SRAM_SUPPORTED_END
#define BOARD_SRAM_DEVICE_SIZE  STM32F429_MAIN_SRAM_DEVICE_SIZE
#define BOARD_SRAM_DEVICE_END   STM32F429_MAIN_SRAM_DEVICE_END

#define BOARD_CCM_BASE          STM32F429_CCM_BASE
#define BOARD_CCM_SIZE          STM32F429_CCM_SIZE

/*---------------------------------------------------------------------------
 * External memories
 *---------------------------------------------------------------------------*/

#define BOARD_SDRAM_BASE        0xD0000000UL
#define BOARD_NAND_BASE         0x70000000UL

/*---------------------------------------------------------------------------
 * Bootloader memory layout
 *---------------------------------------------------------------------------*/

#define BOARD_BOOTLOADER_SIZE   STM32F429_BOOTLOADER_SIZE

#define BOARD_BOOTLOADER_BASE   STM32F429_BOOTLOADER_BASE
#define BOARD_BOOTLOADER_END    STM32F429_BOOTLOADER_END

#define BOARD_SIGNED_IMAGE_BASE STM32F429_SIGNED_IMAGE_BASE
#define BOARD_APPLICATION_BASE  STM32F429_APPLICATION_BASE

#endif
