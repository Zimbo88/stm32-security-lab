# Memory Layout

The authoritative STM32F429 layout profiles are
`config/stm32f429_memory_layout.json`. The default generated profile is
`stm32f429_1m`, the hardware-validation profile for the STM32F429IGT6 with
exactly 1 MiB of internal flash. The old 2 MiB map is retained only as the
explicit `stm32f429_2m` legacy/reference profile; it is not emitted into the
firmware build by default.

Generated consumers are:

- `firmware/common/stm32f429_memory_layout.h`
- `firmware/common/stm32f429_memory_layout.ld`
- `firmware/common/stm32f429_memory_layout.mk`
- `tools/stm32f429_layout.py`

Run `python3 tools/emit_memory_layout.py --check` to detect stale generated
layout files for the selected default profile. All flash ranges below are
half-open: the end address is exclusive. Erase regions are complete STM32F429
sectors only.

## Physical Flash

The STM32F429IGT6 hardware-validation profile is 1 MiB at
`0x08000000-0x08100000`. Only sectors 0 through 11 exist in this profile.
Sector IDs 12 through 23 are invalid and are rejected by generated layout tests
and the target flash backend model.

| Sector | Bank | Range | Size | Use |
|---:|---:|---|---:|---|
| S0 | 1 | `0x08000000-0x08004000` | 16 KiB | Stage 0 |
| S1 | 1 | `0x08004000-0x08008000` | 16 KiB | Stage 0 |
| S2 | 1 | `0x08008000-0x0800c000` | 16 KiB | Boot metadata copy A |
| S3 | 1 | `0x0800c000-0x08010000` | 16 KiB | Boot metadata copy B |
| S4 | 1 | `0x08010000-0x08020000` | 64 KiB | Update metadata |
| S5 | 1 | `0x08020000-0x08040000` | 128 KiB | Slot A |
| S6 | 1 | `0x08040000-0x08060000` | 128 KiB | Slot A |
| S7 | 1 | `0x08060000-0x08080000` | 128 KiB | Slot A |
| S8 | 1 | `0x08080000-0x080a0000` | 128 KiB | Slot B |
| S9 | 1 | `0x080a0000-0x080c0000` | 128 KiB | Slot B |
| S10 | 1 | `0x080c0000-0x080e0000` | 128 KiB | Slot B |
| S11 | 1 | `0x080e0000-0x08100000` | 128 KiB | Reserved recovery |

## Named Regions

| Region | Range | Size | Sectors |
|---|---|---:|---|
| Stage 0 | `0x08000000-0x08008000` | 32 KiB | S0-S1 |
| Metadata copy A | `0x08008000-0x0800c000` | 16 KiB | S2 |
| Metadata copy B | `0x0800c000-0x08010000` | 16 KiB | S3 |
| Update metadata | `0x08010000-0x08020000` | 64 KiB | S4 |
| Slot A signed image | `0x08020000-0x08080000` | 384 KiB | S5-S7 |
| Slot B signed image | `0x08080000-0x080e0000` | 384 KiB | S8-S10 |
| Reserved recovery | `0x080e0000-0x08100000` | 128 KiB | S11 |

Slot A and Slot B each contain the canonical 512-byte signed-image header.
The application vector table starts at the payload base.

| Slot | Header base | Manifest | Signature | Payload/vector base | End | Max payload |
|---|---|---|---|---|---|---:|
| A | `0x08020000` | `0x08020000` | `0x08020060` | `0x08020200` | `0x08080000` | `0x0005fe00` |
| B | `0x08080000` | `0x08080000` | `0x08080060` | `0x08080200` | `0x080e0000` | `0x0005fe00` |

Legacy single-image constants intentionally alias Slot A until Stage-0 slot
selection is implemented. The trusted public key, Ed25519 verification,
SHA-512 payload hashing, and canonical 512-byte signed-image header remain
unchanged.

## SRAM

| Region | Address range | Size | Status |
|---|---|---:|---|
| Main SRAM device range | `0x20000000-0x20040000` | 256 KiB | Documented device capacity |
| Supported application SRAM | `0x20000000-0x20020000` | 128 KiB | Linker RAM and accepted MSP range |
| Additional main SRAM | `0x20020000-0x20040000` | 128 KiB | Reserved/unsupported by this baseline |
| CCM RAM | `0x10000000-0x10010000` | 64 KiB | Not accepted for application MSP/execution |

The accepted initial MSP range is `(0x20000000, 0x20020000]` and must be
8-byte aligned. SRAM execution is not supported by the current verifier.

## Build Checks

`tools/stm32f429_layout.py` rejects stale or unsafe geometry, including
overlaps, unexpected gaps, slot-capacity mismatch, slot payload bases that are
not VTOR-aligned, metadata regions that are not sector-aligned, bank-boundary
drift, sector IDs above 11 in the 1 MiB profile, and Stage-0 growth beyond
S0-S1. The EXP045 bootloader size check still fails if the binary exceeds the
reserved 32 KiB Stage-0 region.

EXP045, EXP065, and EXP066 linker scripts assert that hardware-validation
builds use `stm32f429_1m` and that no load image extends past `0x08100000`.
EXP066 Slot A and Slot B builds place vector tables at `0x08020200` and
`0x08080200`, respectively.
