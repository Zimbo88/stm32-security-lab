# Memory Layout

The authoritative STM32F429IGT6 layout is
`config/stm32f429_memory_layout.json`. Generated consumers are:

- `firmware/common/stm32f429_memory_layout.h`
- `firmware/common/stm32f429_memory_layout.ld`
- `firmware/common/stm32f429_memory_layout.mk`

Run `python3 tools/emit_memory_layout.py --check` to detect stale generated
layout files.

## Flash

| Region | Address range | Size | Status |
|---|---|---:|---|
| Device flash | `0x08000000`-`0x081fffff` | 2 MiB | Documented device capacity |
| Flash bank 1 | `0x08000000`-`0x080fffff` | 1 MiB | Supported executable flash |
| Bootloader | `0x08000000`-`0x08007fff` | 32 KiB | Stage 0 |
| Manifest | `0x08008000`-`0x0800805f` | 96 bytes | Signed metadata |
| Ed25519 signature | `0x08008060`-`0x0800809f` | 64 bytes | Signature over manifest |
| Signed-image padding | `0x080080a0`-`0x080081ff` | 352 bytes | Filled with `0xff` by signer |
| Application payload | `0x08008200`-`0x080fffff` | `0x000f7e00` bytes | Linked and verified application region |
| Flash bank 2 | `0x08100000`-`0x081fffff` | 1 MiB | Reserved/unsupported by this baseline |

The bootloader accepts an application payload only if the complete manifest
payload range fits inside `0x08008200` through `0x080fffff`.

## SRAM

| Region | Address range | Size | Status |
|---|---|---:|---|
| Main SRAM device range | `0x20000000`-`0x2003ffff` | 256 KiB | Documented device capacity |
| Supported application SRAM | `0x20000000`-`0x2001ffff` | 128 KiB | Linker RAM and accepted MSP range |
| Additional main SRAM | `0x20020000`-`0x2003ffff` | 128 KiB | Reserved/unsupported by this baseline |
| CCM RAM | `0x10000000`-`0x1000ffff` | 64 KiB | Not accepted for application MSP/execution |

The accepted initial MSP range is `(0x20000000, 0x20020000]` and must be
8-byte aligned. SRAM execution is not supported by the current verifier.

## Signed Image Structure

| Offset | Size | Content |
|---:|---:|---|
| `0x000` | `0x060` | Manifest |
| `0x060` | `0x040` | Ed25519 signature |
| `0x0a0` | `0x160` | Reserved padding |
| `0x200` | variable | Application payload and vector table |

## Linker Alignment

The EXP045 bootloader, EXP065 signed app, and EXP066 research platform linkers
include `firmware/common/stm32f429_memory_layout.ld`. Regression tests fail if
the generated layout drifts or if the EXP066 vector table no longer resolves to
`0x08008200`.

The EXP066 Cortex-M4 vector table now uses the architectural exception order:

| Index | Vector |
|---:|---|
| 0 | Initial MSP |
| 1 | Reset |
| 2 | NMI |
| 3 | HardFault |
| 4 | MemManage |
| 5 | BusFault |
| 6 | UsageFault |
| 7-10 | Reserved zero |
| 11 | SVCall |
| 12 | DebugMon |
| 13 | Reserved zero |
| 14 | PendSV |
| 15 | SysTick |
