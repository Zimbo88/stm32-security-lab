# EXP039 – CoreSight Component Identification

## Summary

- Total probes: **100**
- Successful reads: **100**
- Failed reads: **0**
- Non-zero ROM-table entries: **6**
- Completion marker present: **yes**

## Component identification

| Component | CID0–CID3 | PID4–PID7, PID0–PID3 | Complete CID | Complete PID |
|---|---|---|---:|---:|
| `ITM` | `0xd 0xe0 0x5 0xb1` | `0x4 0x0 0x0 0x0 0x1 0xb0 0x3b 0x0` | **yes** | **yes** |
| `DWT` | `0xd 0xe0 0x5 0xb1` | `0x4 0x0 0x0 0x0 0x2 0xb0 0x3b 0x0` | **yes** | **yes** |
| `FPB` | `0xd 0xe0 0x5 0xb1` | `0x4 0x0 0x0 0x0 0x3 0xb0 0x2b 0x0` | **yes** | **yes** |
| `SCS` | `0xd 0xe0 0x5 0xb1` | `0x4 0x0 0x0 0x0 0xc 0xb0 0xb 0x0` | **yes** | **yes** |
| `TPIU` | `0xd 0x90 0x5 0xb1` | `0x4 0x0 0x0 0x0 0xa1 0xb9 0xb 0x0` | **yes** | **yes** |
| `ETM` | `0xd 0x90 0x5 0xb1` | `0x4 0x0 0x0 0x0 0x25 0xb9 0xb 0x0` | **yes** | **yes** |
| `ROMTABLE` | `0xd 0x10 0x5 0xb1` | `0x0 0x0 0x0 0x0 0x11 0x4 0xa 0x0` | **yes** | **yes** |

## Fully identified components

- `ITM`
- `DWT`
- `FPB`
- `SCS`
- `TPIU`
- `ETM`
- `ROMTABLE`

## Partially identified components

- None

## Components without complete identification

- None

## ROM-table entries

| Entry | Address | Value |
|---|---:|---:|
| `ROM_ENTRY_00` | `0xE00FF000` | `0xfff0f003` |
| `ROM_ENTRY_01` | `0xE00FF004` | `0xfff02003` |
| `ROM_ENTRY_02` | `0xE00FF008` | `0xfff03003` |
| `ROM_ENTRY_03` | `0xE00FF00C` | `0xfff01003` |
| `ROM_ENTRY_04` | `0xE00FF010` | `0xfff41003` |
| `ROM_ENTRY_05` | `0xE00FF014` | `0xfff42003` |
| `ROM_ENTRY_06` | `0xE00FF018` | `0x0` |
| `ROM_ENTRY_07` | `0xE00FF01C` | `0x0` |
| `ROM_ENTRY_08` | `0xE00FF020` | `0x0` |
| `ROM_ENTRY_09` | `0xE00FF024` | `0x0` |
| `ROM_ENTRY_10` | `0xE00FF028` | `0x0` |
| `ROM_ENTRY_11` | `0xE00FF02C` | `0x0` |
| `ROM_ENTRY_12` | `0xE00FF030` | `0x0` |
| `ROM_ENTRY_13` | `0xE00FF034` | `0x0` |
| `ROM_ENTRY_14` | `0xE00FF038` | `0x0` |
| `ROM_ENTRY_15` | `0xE00FF03C` | `0x0` |

## Non-zero ROM-table entries

- `ROM_ENTRY_00`
- `ROM_ENTRY_01`
- `ROM_ENTRY_02`
- `ROM_ENTRY_03`
- `ROM_ENTRY_04`
- `ROM_ENTRY_05`

## Interpretation

All seven examined CoreSight component windows exposed complete PID and CID
register sets. The first six ROM-table entries were non-zero; the remaining
ten entries were zero.

Readable identification registers demonstrate that the corresponding debug
component windows are accessible. They do not by themselves prove that every
functional feature is enabled or actively usable.

## Safety

All explicit target accesses were read-only. No SRAM writes, flash erase,
flash programming or option-byte modifications were performed.
