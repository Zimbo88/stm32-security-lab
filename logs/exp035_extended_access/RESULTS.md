# EXP035 – Extended Read-Only Access Survey

## Summary

- Total probes: **42**
- Successful reads: **21**
- Failed reads: **21**
- Completion marker present: **yes**

## Access matrix

| Test | Address | Width | Status | Value |
|---|---:|---:|---:|---:|
| `SCB_CPUID` | `0xE000ED00` | 32 | **OK** | `0x410fc241` |
| `SCB_VTOR` | `0xE000ED08` | 32 | **OK** | `0x0` |
| `DBGMCU_IDCODE` | `0xE0042000` | 32 | **OK** | `0x20036419` |
| `FLASH_OPTCR` | `0x40023C14` | 32 | **OK** | `0xffe00ed` |
| `ALIAS_START_32` | `0x00000000` | 32 | **FAIL** | — |
| `ALIAS_START_16` | `0x00000000` | 16 | **FAIL** | — |
| `ALIAS_START_8` | `0x00000000` | 8 | **FAIL** | — |
| `FLASH_000000_32` | `0x08000000` | 32 | **FAIL** | — |
| `FLASH_000000_16` | `0x08000000` | 16 | **FAIL** | — |
| `FLASH_000000_8` | `0x08000000` | 8 | **FAIL** | — |
| `FLASH_004000` | `0x08004000` | 32 | **FAIL** | — |
| `FLASH_010000` | `0x08010000` | 32 | **FAIL** | — |
| `FLASH_020000` | `0x08020000` | 32 | **FAIL** | — |
| `FLASH_040000` | `0x08040000` | 32 | **FAIL** | — |
| `FLASH_080000` | `0x08080000` | 32 | **FAIL** | — |
| `FLASH_100000` | `0x08100000` | 32 | **FAIL** | — |
| `SRAM_20000000` | `0x20000000` | 32 | **OK** | `0x769df8ae` |
| `SRAM_20001000` | `0x20001000` | 32 | **OK** | `0xbbb453df` |
| `SRAM_20010000` | `0x20010000` | 32 | **OK** | `0xa5fadda8` |
| `SRAM_2001BFFC` | `0x2001BFFC` | 32 | **OK** | `0x403080c6` |
| `SRAM_2001C000` | `0x2001C000` | 32 | **OK** | `0x883f5c7a` |
| `SRAM_2001FFFC` | `0x2001FFFC` | 32 | **OK** | `0x8008269` |
| `SRAM_20020000` | `0x20020000` | 32 | **OK** | `0x862f1c9e` |
| `SRAM_2002FFFC` | `0x2002FFFC` | 32 | **OK** | `0x12d01d1` |
| `CCM_START` | `0x10000000` | 32 | **OK** | `0x52d24144` |
| `CCM_MIDDLE` | `0x10008000` | 32 | **OK** | `0xd62dcdda` |
| `CCM_END` | `0x1000FFFC` | 32 | **OK** | `0xb150468e` |
| `SYSTEM_START` | `0x1FFF0000` | 32 | **FAIL** | — |
| `SYSTEM_MIDDLE` | `0x1FFF4000` | 32 | **FAIL** | — |
| `UID0_32` | `0x1FFF7A10` | 32 | **FAIL** | — |
| `UID0_16` | `0x1FFF7A10` | 16 | **FAIL** | — |
| `UID0_8` | `0x1FFF7A10` | 8 | **FAIL** | — |
| `UID1` | `0x1FFF7A14` | 32 | **FAIL** | — |
| `UID2` | `0x1FFF7A18` | 32 | **FAIL** | — |
| `FLASH_SIZE_16` | `0x1FFF7A22` | 16 | **FAIL** | — |
| `FLASH_SIZE_8` | `0x1FFF7A22` | 8 | **FAIL** | — |
| `RCC_CR` | `0x40023800` | 32 | **OK** | `0x5f83` |
| `RCC_CSR` | `0x40023874` | 32 | **OK** | `0x1e000000` |
| `PWR_CR` | `0x40007000` | 32 | **OK** | `0xc000` |
| `SYSCFG_MEMRMP` | `0x40013800` | 32 | **OK** | `0x0` |
| `GPIOA_MODER` | `0x40020000` | 32 | **OK** | `0xa8000000` |
| `GPIOB_MODER` | `0x40020400` | 32 | **OK** | `0x280` |

## Interpretation boundary

The results describe access through the current ST-Link, OpenOCD and target
protection configuration.

A failed read is recorded as an observed access failure. It is not by itself
proof of one specific hardware cause.

## Safety

All target accesses used OpenOCD `read_memory`.

No memory write, flash erase, flash programming or option-byte modification
was performed.
