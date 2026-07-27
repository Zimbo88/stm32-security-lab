# EXP045 Bootloader V2

EXP045 is the current Stage-0 secure-boot research bootloader for the
STM32F429 target. It selects Slot A or Slot B from redundant boot metadata,
verifies the selected signed image in place, and transfers control only to an
accepted application vector table inside the verified slot payload.

This is a laboratory baseline, not a production-ready secure boot chain.

## Implemented Checks

- 96-byte little-endian signed-image manifest
- Manifest magic `0x31474953`
- Manifest header version `2` for update-slot packages
- Minimum image version `2`
- Zero unsupported manifest flags
- Update target compatibility `0xF429AB01`
- Application image type `1`
- Slot-specific application vector address
- Overflow-checked payload address range
- Payload fully inside the selected Slot A or Slot B payload range
- Initial MSP inside the supported main SRAM range and 8-byte aligned
- Thumb reset vector inside the accepted payload
- SHA-512 payload digest
- Ed25519 signature over the manifest
- Rollback floor before accepting candidate firmware
- Candidate flash readback and installed-image verification after updates

The verifier fails closed. Any failed check halts the bootloader instead of
starting the application. After cryptographic verification succeeds, Stage 0
re-decodes the manifest, rechecks the payload bounds, re-reads the application
vector table, and validates the prepared jump context before changing `VTOR`
or `MSP`.

The final jump path disables interrupts, stops SysTick, clears NVIC enable and
pending state, updates `VTOR`, executes data/instruction barriers around the
handoff, loads the validated MSP, and transfers control to the Thumb reset
vector. If the final jump context fails validation or application entry
unexpectedly returns, the bootloader reports the failure and halts.

## Memory Layout

The authoritative STM32F429 layout is defined in
`config/stm32f429_memory_layout.json` and emitted into
`firmware/common/stm32f429_memory_layout.h`,
`firmware/common/stm32f429_memory_layout.ld`, and
`firmware/common/stm32f429_memory_layout.mk`.

| Region | Address range |
|---|---|
| Bootloader | `0x08000000`-`0x08007fff` |
| Boot metadata copy A | `0x08008000`-`0x0800bfff` |
| Boot metadata copy B | `0x0800c000`-`0x0800ffff` |
| Update metadata | `0x08010000`-`0x0801ffff` |
| Slot A signed image | `0x08020000`-`0x0807ffff` |
| Slot A payload/vector base | `0x08020200` |
| Slot B signed image | `0x08080000`-`0x080dffff` |
| Slot B payload/vector base | `0x08080200` |
| Reserved recovery | `0x080e0000`-`0x080fffff` |
| Supported application SRAM | `0x20000000`-`0x2001ffff` |

CCM RAM and the remainder of device SRAM are documented but are not accepted as
application execution or initial-MSP regions by this baseline.

Each slot starts with the canonical 512-byte signed-image header:

| Field | Offset | Size |
|---|---:|---:|
| Manifest | `0x000` | 96 bytes |
| Ed25519 signature | `0x060` | 64 bytes |
| Header padding | `0x0a0` | 352 bytes |
| Payload/vector table | `0x200` | Slot-limited |

## Build

```sh
make -C firmware/exp045_bootloader_v2 clean all
```

The build uses `-Wall -Wextra -Werror` and a linker assertion that keeps the
bootloader inside the reserved 32 KiB flash region.

## Host Verification Tests

The production C verifier, update installer, UART transport, update protocol,
diagnostic console, and update metadata paths are exercised by host tests.

```sh
make -C tests/host_verifier clean test
make -C tests/host_verifier clean test SANITIZE=1
make -C tests/update_storage clean test
make -C tests/update_storage clean test SANITIZE=1
make -C tests/update_protocol clean test
make -C tests/update_protocol clean test SANITIZE=1
make -C tests/uart clean test
make -C tests/diagnostic_console clean test
```

The host tests verify parser, cryptographic verifier, malformed-image,
boundary-value, metadata, update, UART, and host-mode jump-context validation
behavior. They do not prove hardware reset behavior, VTOR relocation on
silicon, option-byte policy, flash protection, power-loss behavior, glitch
resistance, or physical recovery.

## Remaining Security Boundaries

The following remain incomplete and must not be represented as production
security properties:

- physical recovery mechanism
- hardware-backed monotonic rollback counter
- bootloader write protection and readout-protection provisioning
- debug locking and option-byte programming workflow
- fault-injection, voltage/clock glitch, and side-channel resistance
- production key generation, storage, rotation, and revocation process
