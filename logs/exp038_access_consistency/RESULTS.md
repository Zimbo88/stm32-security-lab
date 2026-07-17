# EXP038 – Access Consistency

## Summary

- Runs: **5**
- Probes per run: **27**
- Consistently readable probes: **19**
- Consistently unreadable probes: **8**
- Probes with unstable accessibility: **0**
- Readable probes with changing values: **0**
- All completion markers present: **yes**

## Consistently readable

- `SCB_CPUID`
- `SCB_VTOR`
- `COREDEBUG_DHCSR`
- `DBGMCU_IDCODE`
- `FLASH_SR`
- `FLASH_CR`
- `FLASH_OPTCR`
- `FLASH_OPTCR1`
- `RCC_CR`
- `RCC_CFGR`
- `RCC_CSR`
- `PWR_CR`
- `SYSCFG_MEMRMP`
- `GPIOA_MODER`
- `SRAM_START`
- `SRAM_MIDDLE`
- `SRAM_END`
- `CCM_START`
- `CCM_END`

## Consistently unreadable

- `FLASH_START`
- `FLASH_VECTOR_1`
- `FLASH_ALIAS`
- `SYSTEM_ROM`
- `UID0`
- `UID1`
- `UID2`
- `FLASH_SIZE`

## Unstable accessibility

- None

## Readable probes with changing values

- None

## Full comparison

| Probe | Address | Five runs | Status stable | Value stable |
|---|---:|---|---:|---:|
| `SCB_CPUID` | `0xE000ED00` | `0x410fc241 / 0x410fc241 / 0x410fc241 / 0x410fc241 / 0x410fc241` | **yes** | **yes** |
| `SCB_VTOR` | `0xE000ED08` | `0x0 / 0x0 / 0x0 / 0x0 / 0x0` | **yes** | **yes** |
| `COREDEBUG_DHCSR` | `0xE000EDF0` | `0x30003 / 0x30003 / 0x30003 / 0x30003 / 0x30003` | **yes** | **yes** |
| `DBGMCU_IDCODE` | `0xE0042000` | `0x20036419 / 0x20036419 / 0x20036419 / 0x20036419 / 0x20036419` | **yes** | **yes** |
| `FLASH_SR` | `0x40023C0C` | `0x0 / 0x0 / 0x0 / 0x0 / 0x0` | **yes** | **yes** |
| `FLASH_CR` | `0x40023C10` | `0x80000000 / 0x80000000 / 0x80000000 / 0x80000000 / 0x80000000` | **yes** | **yes** |
| `FLASH_OPTCR` | `0x40023C14` | `0xffe00ed / 0xffe00ed / 0xffe00ed / 0xffe00ed / 0xffe00ed` | **yes** | **yes** |
| `FLASH_OPTCR1` | `0x40023C18` | `0xfff0000 / 0xfff0000 / 0xfff0000 / 0xfff0000 / 0xfff0000` | **yes** | **yes** |
| `RCC_CR` | `0x40023800` | `0x5f83 / 0x5f83 / 0x5f83 / 0x5f83 / 0x5f83` | **yes** | **yes** |
| `RCC_CFGR` | `0x40023808` | `0x0 / 0x0 / 0x0 / 0x0 / 0x0` | **yes** | **yes** |
| `RCC_CSR` | `0x40023874` | `0x1e000000 / 0x1e000000 / 0x1e000000 / 0x1e000000 / 0x1e000000` | **yes** | **yes** |
| `PWR_CR` | `0x40007000` | `0xc000 / 0xc000 / 0xc000 / 0xc000 / 0xc000` | **yes** | **yes** |
| `SYSCFG_MEMRMP` | `0x40013800` | `0x0 / 0x0 / 0x0 / 0x0 / 0x0` | **yes** | **yes** |
| `GPIOA_MODER` | `0x40020000` | `0xa8000000 / 0xa8000000 / 0xa8000000 / 0xa8000000 / 0xa8000000` | **yes** | **yes** |
| `SRAM_START` | `0x20000000` | `0x569df8ae / 0x569df8ae / 0x569df8ae / 0x569df8ae / 0x569df8ae` | **yes** | **yes** |
| `SRAM_MIDDLE` | `0x20010000` | `0xa5fadca8 / 0xa5fadca8 / 0xa5fadca8 / 0xa5fadca8 / 0xa5fadca8` | **yes** | **yes** |
| `SRAM_END` | `0x2002FFFC` | `0x16d21c1 / 0x16d21c1 / 0x16d21c1 / 0x16d21c1 / 0x16d21c1` | **yes** | **yes** |
| `CCM_START` | `0x10000000` | `0x52d24dc0 / 0x52d24dc0 / 0x52d24dc0 / 0x52d24dc0 / 0x52d24dc0` | **yes** | **yes** |
| `CCM_END` | `0x1000FFFC` | `0x9150468e / 0x9150468e / 0x9150468e / 0x9150468e / 0x9150468e` | **yes** | **yes** |
| `FLASH_START` | `0x08000000` | `FAIL / FAIL / FAIL / FAIL / FAIL` | **yes** | **no** |
| `FLASH_VECTOR_1` | `0x08000004` | `FAIL / FAIL / FAIL / FAIL / FAIL` | **yes** | **no** |
| `FLASH_ALIAS` | `0x00000000` | `FAIL / FAIL / FAIL / FAIL / FAIL` | **yes** | **no** |
| `SYSTEM_ROM` | `0x1FFF0000` | `FAIL / FAIL / FAIL / FAIL / FAIL` | **yes** | **no** |
| `UID0` | `0x1FFF7A10` | `FAIL / FAIL / FAIL / FAIL / FAIL` | **yes** | **no** |
| `UID1` | `0x1FFF7A14` | `FAIL / FAIL / FAIL / FAIL / FAIL` | **yes** | **no** |
| `UID2` | `0x1FFF7A18` | `FAIL / FAIL / FAIL / FAIL / FAIL` | **yes** | **no** |
| `FLASH_SIZE` | `0x1FFF7A22` | `FAIL / FAIL / FAIL / FAIL / FAIL` | **yes** | **no** |

## Interpretation boundary

Stable accessibility means that the read operation returned the same status
in all five runs. A changing value in a readable register is not necessarily
an error; dynamic peripheral and reset-state registers may legitimately
change between runs.

## Safety

All explicit target memory accesses were read-only. No RAM writes, flash
erase, flash programming or option-byte modifications were performed.
