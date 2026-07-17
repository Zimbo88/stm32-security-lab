# EXP037 – Reset-State Comparison

## Summary

- Snapshots: **3**
- Probes per snapshot: **33**
- Registers with differing rendered values: **8**
- Failed register reads: **3**
- All completion markers present: **yes**

## Snapshot sequence

1. Attach and halt without reset
2. Reset and halt
3. Physical power-cycle followed by reset and halt

## Comparison

| Register | Attach/halt | Reset/halt | Power-cycle | Changed |
|---|---:|---:|---:|---:|
| `CORE_PC` | `0xfffffffe` | `0xfffffffe` | `0xfffffffe` | **no** |
| `CORE_MSP` | `0x2001ffc0` | `0xfffffffc` | `0xfffffffc` | **yes** |
| `CORE_PSP` | `0x00000000` | `0x00000000` | `0x00000000` | **no** |
| `CORE_XPSR` | `FAIL` | `FAIL` | `FAIL` | **no** |
| `CORE_CONTROL` | `0x00` | `0x00` | `0x00` | **no** |
| `CORE_PRIMASK` | `0x00` | `0x00` | `0x00` | **no** |
| `CORE_BASEPRI` | `0x00` | `0x00` | `0x00` | **no** |
| `CORE_FAULTMASK` | `0x00` | `0x01` | `0x01` | **yes** |
| `SCB_CPUID` | `0x410fc241` | `0x410fc241` | `0x410fc241` | **no** |
| `SCB_ICSR` | `0x803` | `0x3000` | `0x3000` | **yes** |
| `SCB_VTOR` | `0x8008200` | `0x0` | `0x0` | **yes** |
| `SCB_AIRCR` | `0xfa050000` | `0xfa050000` | `0xfa050000` | **no** |
| `SCB_SCR` | `0x0` | `0x0` | `0x0` | **no** |
| `SCB_CCR` | `0x200` | `0x200` | `0x200` | **no** |
| `SCB_SHCSR` | `0x0` | `0x0` | `0x0` | **no** |
| `SCB_CFSR` | `0x101` | `0x0` | `0x0` | **yes** |
| `SCB_HFSR` | `0x40000002` | `0x2` | `0x2` | **yes** |
| `SCB_DFSR` | `0x1` | `0x8` | `0x8` | **yes** |
| `SCB_MMFAR` | `0xe000edf8` | `0xe000edf8` | `0xe000edf8` | **no** |
| `SCB_BFAR` | `0xe000edf8` | `0xe000edf8` | `0xe000edf8` | **no** |
| `SCB_AFSR` | `0x0` | `0x0` | `0x0` | **no** |
| `COREDEBUG_DHCSR` | `0x30003` | `0x30003` | `0x30003` | **no** |
| `COREDEBUG_DEMCR` | `0x1000000` | `0x1000000` | `0x1000000` | **no** |
| `DBGMCU_IDCODE` | `0x20036419` | `0x20036419` | `0x20036419` | **no** |
| `FLASH_SR` | `0x0` | `0x0` | `0x0` | **no** |
| `FLASH_CR` | `0x80000000` | `0x80000000` | `0x80000000` | **no** |
| `FLASH_OPTCR` | `0xffe00ed` | `0xffe00ed` | `0xffe00ed` | **no** |
| `FLASH_OPTCR1` | `0xfff0000` | `0xfff0000` | `0xfff0000` | **no** |
| `RCC_CR` | `0x5f83` | `0x5f83` | `0x5f83` | **no** |
| `RCC_CFGR` | `0x0` | `0x0` | `0x0` | **no** |
| `RCC_CSR` | `0xe000000` | `0x1e000000` | `0x1e000000` | **yes** |
| `PWR_CR` | `0xc000` | `0xc000` | `0xc000` | **no** |
| `SYSCFG_MEMRMP` | `0x0` | `0x0` | `0x0` | **no** |

## Registers with differing values

- `CORE_MSP`
- `CORE_FAULTMASK`
- `SCB_ICSR`
- `SCB_VTOR`
- `SCB_CFSR`
- `SCB_HFSR`
- `SCB_DFSR`
- `RCC_CSR`

## Failed register reads

- `A_ATTACH_HALT`: `CORE_XPSR`
- `B_RESET_HALT`: `CORE_XPSR`
- `C_POWER_CYCLE_RESET_HALT`: `CORE_XPSR`

## Interpretation boundary

A differing raw value records a state difference between snapshots. It does
not by itself establish the cause.

MMFAR and BFAR are valid fault addresses only when the corresponding validity
bits in CFSR are set.

## Safety

OpenOCD target-control commands were used to halt and reset the processor.
All explicit target data accesses were read-only. No SRAM write, flash erase,
flash programming or option-byte modification was performed.
