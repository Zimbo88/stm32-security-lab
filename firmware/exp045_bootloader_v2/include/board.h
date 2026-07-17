#ifndef BOARD_H
#define BOARD_H

#include <stdint.h>

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

#define BOARD_FLASH_BASE        0x08000000UL
#define BOARD_FLASH_SIZE        (2UL * 1024UL * 1024UL)

#define BOARD_FLASH_BANK1_BASE  0x08000000UL
#define BOARD_FLASH_BANK2_BASE  0x08100000UL
#define BOARD_FLASH_END         (BOARD_FLASH_BASE + BOARD_FLASH_SIZE)

#define BOARD_SRAM_BASE         0x20000000UL
#define BOARD_SRAM_SIZE         (256UL * 1024UL)

#define BOARD_CCM_BASE          0x10000000UL
#define BOARD_CCM_SIZE          (64UL * 1024UL)

/*---------------------------------------------------------------------------
 * External memories
 *---------------------------------------------------------------------------*/

#define BOARD_SDRAM_BASE        0xD0000000UL
#define BOARD_NAND_BASE         0x70000000UL

/*---------------------------------------------------------------------------
 * Bootloader memory layout
 *---------------------------------------------------------------------------*/

#define BOARD_BOOTLOADER_SIZE   (32UL * 1024UL)

#define BOARD_BOOTLOADER_BASE   BOARD_FLASH_BASE
#define BOARD_BOOTLOADER_END    (BOARD_BOOTLOADER_BASE + BOARD_BOOTLOADER_SIZE)

#define BOARD_SIGNED_IMAGE_BASE BOARD_BOOTLOADER_END
#define BOARD_APPLICATION_BASE  0x08008200UL

#endif
