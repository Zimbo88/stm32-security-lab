# EXP034 – Memory Access Matrix

## Objective

Determine which representative memory regions are readable through the
current SWD debug connection and protection state.

## Results

| Test | Address | Width | Status | Value |
|---|---:|---:|---:|---:|
| `CORE_CPUID` | `0xE000ED00` | 32 | **OK** | `0x410fc241` |
| `DBGMCU_IDCODE` | `0xE0042000` | 32 | **OK** | `0x20036419` |
| `FLASH_OPTCR` | `0x40023C14` | 32 | **OK** | `0xffe00ed` |
| `SRAM_START` | `0x20000000` | 32 | **OK** | `0x769df8ae` |
| `FLASH_START` | `0x08000000` | 32 | **FAIL** | — |
| `SYSTEM_ROM_START` | `0x1FFF0000` | 32 | **FAIL** | — |
| `UID0` | `0x1FFF7A10` | 32 | **FAIL** | — |
| `UID1` | `0x1FFF7A14` | 32 | **FAIL** | — |
| `UID2` | `0x1FFF7A18` | 32 | **FAIL** | — |
| `FLASH_SIZE` | `0x1FFF7A22` | 16 | **FAIL** | — |

## Observations

- ARM system-control registers are readable.
- DBGMCU and FLASH controller registers are readable.
- SRAM is readable.
- Internal flash at `0x08000000` was not readable.
- System memory at `0x1FFF0000` was not readable.
- UID words at `0x1FFF7A10` through `0x1FFF7A18` were not readable.
- The flash-size value at `0x1FFF7A22` was not readable.
- The completion marker was reached despite individual read failures.

## Interpretation boundary

The report records observed access results under the current target,
debug-probe, OpenOCD and protection configuration.

It does not by itself establish whether every failed access is caused solely
by RDP, by the target state, or by the debug access implementation.

## Safety

Only OpenOCD `read_memory` operations were used.

No SRAM write, flash programming, flash erase or option-byte modification was
performed.
