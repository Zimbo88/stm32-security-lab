# STM32 Secure Boot Research Platform

## Overview

This repository contains a secure boot research platform for the STM32F429.

The project separates a trusted bootloader from a signed research application.
Before transferring control, the bootloader validates the firmware image,
enforces a minimum image version, checks the application vector table, and
rejects invalid images using a fail-safe policy.

The implementation is intended as a practical research artifact accompanying a
master's thesis on secure boot mechanisms for embedded systems.

## Main Features

- SHA-512 payload integrity verification
- Ed25519 firmware authentication
- Embedded trusted public key
- Minimum image-version enforcement
- Manifest header-version validation
- Application range and size validation
- Initial MSP validation
- Reset-vector validation
- Deny-by-default boot policy
- Reproducible negative security tests
- Separate bootloader and application projects

## Repository Structure

```text
stm32-security-lab/
├── docs/
│   ├── architecture.md
│   ├── memory_layout.md
│   ├── secure_boot_validation.md
│   └── threat_model.md
├── firmware/
│   ├── exp045_bootloader_v2/
│   ├── exp065_signed_app/
│   └── exp066_research_platform_core/
└── README.md
```

## Components

### EXP045 Bootloader V2

Location:

```text
firmware/exp045_bootloader_v2
```

Responsibilities:

- Parse the signed image manifest
- Validate manifest metadata
- Verify the SHA-512 payload digest
- Verify the Ed25519 signature
- Enforce the image-version policy
- Validate the initial MSP
- Validate the reset vector
- Transfer control only after all checks succeed

### EXP065 Signed Image Tooling

Location:

```text
firmware/exp065_signed_app
```

Responsibilities:

- Build the signed image container
- Calculate the SHA-512 application digest
- Sign the manifest using Ed25519
- Support configurable image versions
- Support configurable manifest header versions
- Reject structurally invalid application binaries before signing

### EXP066 Research Platform Core

Location:

```text
firmware/exp066_research_platform_core
```

Responsibilities:

- Provide the application firmware executed after secure boot
- Provide UART-based platform output
- Provide platform health and fault handling
- Serve as the signed application payload for validation

## Security Architecture

The bootloader is part of the trusted computing base.

The application is treated as untrusted until all configured validation steps
succeed.

The trusted Ed25519 public key is compiled into the bootloader. The
corresponding private signing seed is required only by the host-side signing
process and must not be stored on the target device.

The bootloader uses a deny-by-default policy. Any malformed, corrupted,
outdated, unsupported, incorrectly signed, or structurally invalid image is
rejected before execution.

Detailed architecture documentation is available in:

```text
docs/architecture.md
```

## Flash Memory Layout

| Region | Start Address | Purpose |
|---|---:|---|
| Bootloader | `0x08000000` | Trusted boot code |
| Signed image | `0x08008000` | Manifest, signature, and application |
| Manifest | `0x08008000` | Signed image metadata |
| Signature | `0x08008060` | Ed25519 signature |
| Application | `0x08008200` | Vector table and firmware payload |

Detailed information is available in:

```text
docs/memory_layout.md
```

## Prerequisites

The build environment requires:

- GNU Make
- Python 3
- ARM GNU Toolchain
- Python package providing Ed25519 support for the signing script
- STM32 flashing tool compatible with the target board

The following compiler tools must be available in `PATH`:

```text
arm-none-eabi-gcc
arm-none-eabi-objcopy
arm-none-eabi-size
```

## Build

Build the bootloader:

```bash
make -C firmware/exp045_bootloader_v2 clean
make -C firmware/exp045_bootloader_v2
```

Build the research application:

```bash
make -C firmware/exp066_research_platform_core clean
make -C firmware/exp066_research_platform_core
```

Expected output files:

```text
firmware/exp045_bootloader_v2/build/exp045_bootloader_v2.elf
firmware/exp045_bootloader_v2/build/exp045_bootloader_v2.bin
firmware/exp066_research_platform_core/build/exp066_research_platform_core.elf
firmware/exp066_research_platform_core/build/exp066_research_platform_core.bin
```

## Build a Signed Image

The signing tool supports explicit manifest and image versions.

Example:

```bash
python3 firmware/exp065_signed_app/tools/build_signed_image.py \
  --application firmware/exp066_research_platform_core/build/exp066_research_platform_core.bin \
  --seed firmware/exp065_signed_app/keys/firmware_signing_seed.bin \
  --output firmware/exp065_signed_app/build/exp066_signed.bin \
  --header-version 1 \
  --image-version 2
```

The signing seed path is deployment-specific. Private signing material should
not be committed to the repository.

## Flashing

The bootloader must be flashed at:

```text
0x08000000
```

The signed image must be flashed at:

```text
0x08008000
```

Example commands depend on the selected flashing tool.

For OpenOCD-compatible setups, the general workflow is:

```bash
openocd \
  -f interface/stlink.cfg \
  -f target/stm32f4x.cfg \
  -c "program firmware/exp045_bootloader_v2/build/exp045_bootloader_v2.elf verify reset exit"

openocd \
  -f interface/stlink.cfg \
  -f target/stm32f4x.cfg \
  -c "program firmware/exp065_signed_app/build/exp066_signed.bin 0x08008000 verify reset exit"
```

The exact interface and target configuration may differ depending on the
debugger and board.

## Secure Boot Sequence

The boot process follows this order:

1. MCU reset
2. Bootloader initialization
3. Reset-cause evaluation
4. Boot-mode evaluation
5. Recovery-policy evaluation
6. Manifest parsing
7. Manifest magic validation
8. Header-version validation
9. Image-version validation
10. Application range validation
11. SHA-512 payload verification
12. Ed25519 signature verification
13. Initial MSP validation
14. Reset-vector validation
15. Vector-table relocation
16. Transfer of control to the application

The application is never started after a failed validation step.

## Validation Results

The following tests were performed successfully:

| Test | Result |
|---|---|
| Valid signed image | PASS |
| Modified payload | PASS |
| Modified signature | PASS |
| Unauthorized signing key | PASS |
| Rollback image | PASS |
| Unsupported header version | PASS |
| Invalid initial MSP | PASS |
| Invalid reset vector | PASS |
| Valid image restoration | PASS |

Detailed logs and observations are documented in:

```text
docs/secure_boot_validation.md
```

## Threat Model

The current design mitigates:

- Unauthorized firmware execution
- Payload modification
- Signature modification
- Firmware replacement with an unauthorized key
- Rollback to an older signed image
- Unsupported manifest formats
- Invalid stack-pointer values
- Invalid reset-vector targets

The following topics are currently outside the implementation scope:

- Physical invasive attacks
- Side-channel attacks
- Fault injection
- Secure firmware transport
- Firmware confidentiality
- Hardware-backed monotonic counters
- Production key provisioning and rotation
- Protection against replacement of an unprotected bootloader

The complete threat model is available in:

```text
docs/threat_model.md
```

## Recovery Status

A recovery-policy interface exists, but no physical recovery input or firmware
update transport is currently implemented.

The bootloader therefore reports recovery as unavailable and aborts safely.

## Current Build Sizes

| Component | Size |
|---|---:|
| Bootloader binary | 13,804 bytes |
| Bootloader reserved flash | 32,768 bytes |
| Application text | 6,852 bytes |
| Application BSS | 1,128 bytes |

These values represent the validated build state and may change with future
implementation changes.

## Reproducibility

A clean build should complete without compiler warnings because both firmware
projects use:

```text
-Wall -Wextra -Werror
```

To reproduce the validated build:

```bash
make -C firmware/exp045_bootloader_v2 clean
make -C firmware/exp045_bootloader_v2

make -C firmware/exp066_research_platform_core clean
make -C firmware/exp066_research_platform_core
```

## Documentation

- [Secure Boot Architecture](docs/architecture.md)
- [Memory Layout](docs/memory_layout.md)
- [Threat Model](docs/threat_model.md)
- [Secure Boot Validation](docs/secure_boot_validation.md)

## Research Status

The secure boot validation phase is complete.

Future research and engineering work may include:

- Physical recovery input
- Authenticated firmware update transport
- Bootloader write protection
- MCU readout protection evaluation
- Persistent rollback counters
- Production key provisioning
- Key rotation
- Fault-injection evaluation
- Timing and performance measurements

## License

No license has been assigned yet. All rights remain with the repository owner
until a license file is added.
