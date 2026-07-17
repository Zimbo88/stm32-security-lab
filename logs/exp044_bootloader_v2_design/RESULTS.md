# EXP044 – Bootloader v2 Architecture Design

## Objective

Design a maintainable secure boot system for the laboratory board.
The board shall boot signed firmware, support controlled updates,
and remain reusable after security experiments.

## Trust Anchors

- The bootloader contains public verification keys only.
- Private signing keys remain outside the microcontroller.
- Only correctly signed firmware may be installed or executed.

## Operating Modes

### Normal Boot

- The manifest and payload are fully verified.
- The application is started only after successful verification.
- Any verification failure results in a fail-closed state.

### Authenticated Update

- Updates are fully received before installation begins.
- Length, destination range, image version, hash, and signature are verified.
- Flash erase or programming is allowed only after validation.
- The bootloader flash region is excluded from application updates.

### Physical Recovery

- Recovery mode is enabled only through a documented physical action.
- Examples include holding a recovery button during reset or using a defined jumper.
- Recovery mode accepts signed images only.
- No universal memory-read command is provided.

## Recommended Flash Layout

- 0x08000000–0x08007FFF: bootloader, 32 KiB
- 0x08008000–0x080081FF: manifest and signature area
- 0x08008200–0x080FFFFF: application payload
- Bootloader and application regions must never overlap.

## Verification Order

1. Manifest structure and format version
2. Magic value and supported algorithm
3. Destination address and size limits
4. Image version and rollback policy
5. Initial MSP and reset vector
6. Payload hash
7. Digital signature
8. Authorization for boot or installation

## Rollback Design

- The current compile-time minimum version is retained initially.
- A future persistent rollback floor requires atomic updates.
- Power loss must not corrupt the stored rollback state.
- The accepted minimum version must never be increased before installation succeeds.

## Update Security Rules

- No write operation without a fully validated image header.
- No addresses outside the application region.
- Address-plus-length calculations must be protected against integer overflow.
- Packet size and communication timeouts must be bounded.
- The programmed image must be verified again directly from flash.

## Laboratory Board Reusability

- RDP Level 2 must not be enabled.
- Documented recovery and maintenance remain available.
- Recovery images and hashes are stored under version control.
- Protection changes are performed only in separate, explicitly approved experiments.

## Non-Goals

- No hidden maintenance access.
- No unauthenticated debug or memory access.
- No mechanism for bypassing enabled protection features.

## Result

The bootloader v2 design separates normal boot, authenticated update,
and physically activated recovery.
All executable images remain signature-protected.
EXP044 was performed entirely offline.
