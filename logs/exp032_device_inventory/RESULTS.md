# EXP032 – STM32F429 Read-Only Device Inventory

## Connection


## Core identification

- SCB CPUID: `not read`

## STM32 device identification

- DBGMCU IDCODE: `not read`

## Flash protection state

- FLASH_OPTCR: `not read`
- FLASH_OPTCR1: `not read`

## Reset state

- RCC_CSR: `not read`
- Active reset flags: none decoded

## MPU

- MPU_TYPE: `not read`

## Register inventory

| Register | Address | Value |
|---|---:|---:|

## System-memory observation

A separate read of the flash-size register at `0x1FFF7A22` failed while debug access and peripheral-register access remained available.

The measured FLASH_OPTCR RDP byte is recorded above. No option-byte modification was performed.

## Safety

All recorded accesses used OpenOCD read_memory commands. No erase, programming or option-byte write command was issued.
