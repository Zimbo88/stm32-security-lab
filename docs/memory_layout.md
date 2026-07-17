# Memory Layout

## Flash Layout

| Region | Start Address | Purpose |
|---|---:|---|
| Bootloader | `0x08000000` | Trusted boot code |
| Signed image | `0x08008000` | Manifest, signature, padding, and application |
| Manifest | `0x08008000` | Signed image metadata |
| Signature | `0x08008060` | Ed25519 signature |
| Application vector table | `0x08008200` | Initial MSP and reset vector |
| Application payload | `0x08008200` | EXP066 firmware image |

## Current Sizes

| Component | Size |
|---|---:|
| Bootloader binary | 13,804 bytes |
| Bootloader reserved region | 32,768 bytes |
| Application text | 6,852 bytes |
| Application BSS | 1,128 bytes |
| Combined signed image | 7,364 bytes |

## Signed Image Structure

| Offset | Size | Content |
|---:|---:|---|
| `0x000` | `0x060` | Manifest |
| `0x060` | `0x040` | Ed25519 signature |
| `0x0A0` | `0x160` | Reserved padding |
| `0x200` | variable | Application payload |

## Runtime Addresses

- Manifest address: `0x08008000`
- Signature address: `0x08008060`
- Application base: `0x08008200`

## Separation Rationale

The bootloader occupies a dedicated flash region and does not overlap with the
signed application image.

The application is linked for execution from `0x08008200`.

The fixed image structure allows the bootloader to locate the manifest,
signature, and vector table without dynamic parsing.
