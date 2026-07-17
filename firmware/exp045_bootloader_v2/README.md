# EXP045 Bootloader V2

EXP045 is the current Stage-0 secure-boot research bootloader for the
STM32F429 target. It validates the signed image at `0x08008000` and transfers
control only to an accepted application vector table at `0x08008200`.

This is a laboratory baseline, not a production-ready secure boot chain.

## Implemented Checks

- 96-byte little-endian signed-image manifest
- Manifest magic `0x31474953`
- Manifest header version `1`
- Minimum image version `2`
- Zero unsupported manifest flags
- Zero reserved manifest fields
- Fixed application vector address `0x08008200`
- Overflow-checked payload address range
- Payload fully inside `0x08008200` through `0x080fffff`
- Initial MSP inside the supported main SRAM range and 8-byte aligned
- Thumb reset vector inside the accepted payload
- SHA-512 payload digest
- Ed25519 signature over the manifest

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
| Manifest | `0x08008000`-`0x0800805f` |
| Ed25519 signature | `0x08008060`-`0x0800809f` |
| Signed-image padding | `0x080080a0`-`0x080081ff` |
| Application payload | `0x08008200`-`0x080fffff` |
| Supported application SRAM | `0x20000000`-`0x2001ffff` |

CCM RAM and the remainder of device SRAM are documented but are not accepted as
application execution or initial-MSP regions by this baseline.

## Build

```sh
make -C firmware/exp045_bootloader_v2 clean all
```

The build uses `-Wall -Wextra -Werror` and a linker assertion that keeps the
bootloader inside the reserved 32 KiB flash region.

## Host Verification Tests

The production C verifier is exercised on the host by
`tests/host_verifier`. These tests compile `signed_image.c` with the host
compiler and Monocypher, then verify valid and malformed in-memory image
vectors.

```sh
make -C tests/host_verifier clean test
make -C tests/host_verifier clean test SANITIZE=1
```

The host tests verify parser, cryptographic verifier, malformed-image, boundary
value, and host-mode jump-context validation behavior. They do not prove
hardware reset behavior, VTOR relocation on silicon, option-byte policy, flash
protection, power-loss behavior, glitch resistance, or physical recovery.

## Remaining Security Boundaries

The following remain incomplete and must not be represented as production
security properties:

- authenticated firmware update transport
- physical recovery mechanism
- hardware-backed monotonic rollback counter
- bootloader write protection and readout-protection provisioning
- debug locking and option-byte programming workflow
- fault-injection, voltage/clock glitch, and side-channel resistance
- production key generation, storage, rotation, and revocation process
