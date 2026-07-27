# Release Process

This document describes the reproducible release workflow for the current
laboratory secure-boot and secure-update baseline. It builds, signs, verifies,
and packages artifacts; it does not flash hardware, change Option Bytes,
enable RDP/WRP, or provide production key custody.

## Scope

The release workflow covers:

- EXP045 Stage-0 bootloader build artifacts
- EXP065/EXP066 application build artifacts
- signed-image construction
- authenticated Slot A/Slot B update-package construction
- initial factory boot-metadata construction
- offline signed-image verification
- offline update-package verification
- release manifest generation
- deterministic rebuild comparison

The workflow is host-only. It does not flash hardware by itself. Hardware boot
and secure-update behavior are covered by the separate validation procedure in
`docs/secure-update-hardware-test.md` and summarized in
`docs/release-readiness.md`.

## Build Inputs

The active firmware Makefiles build with:

- sorted source-file ordering
- `-Wall -Wextra -Werror`
- debug-prefix normalization through `-ffile-prefix-map`,
  `-fdebug-prefix-map`, and `-fmacro-prefix-map`
- linker build-id disabled through `-Wl,--build-id=none`
- explicit ELF, BIN, and Intel HEX artifacts

The signing tool is deterministic for identical application bytes, Ed25519
seed, image version, and manifest version.

## Build Firmware

```sh
make -C firmware/exp045_bootloader_v2 clean all
make -C firmware/exp066_research_platform_core clean all
```

The hardware-validation build uses the generated `stm32f429_1m` profile:
STM32F429IGT6, 1 MiB internal flash, sectors 0-11 only, Slot A
`0x08020000-0x08080000`, Slot B `0x08080000-0x080e0000`, and recovery
`0x080e0000-0x08100000`.

Expected bootloader artifacts:

- `firmware/exp045_bootloader_v2/build/exp045_bootloader_v2.elf`
- `firmware/exp045_bootloader_v2/build/exp045_bootloader_v2.bin`
- `firmware/exp045_bootloader_v2/build/exp045_bootloader_v2.hex`

Expected application artifacts:

- `firmware/exp066_research_platform_core/build/exp066_research_platform_core.elf`
- `firmware/exp066_research_platform_core/build/exp066_research_platform_core.bin`
- `firmware/exp066_research_platform_core/build/exp066_research_platform_core.hex`

Build both slot-linked update releases:

```sh
make -C firmware/exp066_research_platform_core slot-releases \
  LAYOUT_PROFILE=stm32f429_1m \
  SIGNING_SEED=/path/to/release_signing_seed.bin \
  PUBLIC_KEY_HEADER=../exp045_bootloader_v2/src/firmware_public_key.h
```

This produces Slot A and Slot B ELF, BIN, HEX, map, update package, package
inspection JSON, and offline verification JSON under:

- `firmware/exp066_research_platform_core/build/slot_a`
- `firmware/exp066_research_platform_core/build/slot_b`

## Sign Firmware

The EXP065 legacy application target still produces the original version-1
signed image:

```sh
make -C firmware/exp065_signed_app signed \
  SIGNING_SEED=/path/to/release_signing_seed.bin
```

EXP066 is verified by EXP045 Stage-0 as a slot-aware update-slot package. Use
the update-package target for factory Slot A and Slot B releases:

```sh
make -C firmware/exp066_research_platform_core SLOT=a update-package \
  LAYOUT_PROFILE=stm32f429_1m \
  SIGNING_SEED=/path/to/release_signing_seed.bin
```

The Slot A output package is:

```text
firmware/exp066_research_platform_core/build/exp066_research_platform_core_slot_a_update_v2.bin
```

The EXP066 compatibility targets `signed` and `verify-signed` are aliases for
the slot-aware update-package build and verification path. They do not produce
the obsolete `exp066_research_platform_core_signed.bin` version-1 artifact.
The signer rejects unsupported manifest versions, unsupported flags, invalid
target compatibility, invalid image type, invalid vector tables, and payloads
outside the selected slot before writing the package.

## Verify Signed Image

Verify the EXP066 update-slot package offline against the public key compiled
into Stage 0:

```sh
make -C firmware/exp066_research_platform_core SLOT=a verify-update-package \
  LAYOUT_PROFILE=stm32f429_1m \
  SIGNING_SEED=/path/to/release_signing_seed.bin \
  PUBLIC_KEY_HEADER=../exp045_bootloader_v2/src/firmware_public_key.h
```

This creates:

```text
firmware/exp066_research_platform_core/build/exp066_research_platform_core_slot_a_package_verify.json
```

The verification report is machine-readable JSON. The command exits with:

- `0` when verification succeeds
- `1` when artifact or signature verification fails
- `2` for command-line usage errors from `argparse`

## Inspect Update Package

Run package inspection and offline verification for the Slot A factory image:

```sh
make -C firmware/exp066_research_platform_core SLOT=a \
  inspect-update-package verify-update-package \
  LAYOUT_PROFILE=stm32f429_1m \
  SIGNING_SEED=/path/to/release_signing_seed.bin \
  PUBLIC_KEY_HEADER=../exp045_bootloader_v2/src/firmware_public_key.h
```

This writes the package inspection JSON and offline verification JSON under
`firmware/exp066_research_platform_core/build/`.

The release manifest includes:

- Git commit and exact tag when available
- compiler identity
- raw Makefile build options
- MCU, selected layout profile, flash size, sector count, and Slot A/Slot B
  base/end addresses
- bootloader version
- application image version
- signed-manifest version
- rollback floor
- SHA-512 for ELF, BIN, HEX, signed image, manifest, signature, and payload
- binary sizes
- Ed25519 signature bytes
- public-key SHA-256 fingerprint
- verification result

## Offline Verification

`tools/release_artifacts.py verify-signed` verifies one legacy version-1 signed
image. `tools/release_artifacts.py verify-release` verifies that legacy
version-1 release artifact set.
EXP066 secure-update-v2 releases are verified with `tools/update_package.py
verify`, which authenticates an update package for a specific slot and target
compatibility identifier.

Both commands parse the manifest using explicit little-endian fields, verify
the payload SHA-512, verify the detached Ed25519 signature over the manifest,
validate vector-table policy, and reject malformed or incomplete artifacts.

The verifier does not require target hardware.

## Factory Metadata Provisioning

After building and verifying the initial Slot A signed image, create the first
redundant `CONFIRMED` metadata records with:

```sh
make -C tools boot-metadata-provision
mkdir -p build/factory
tools/build/boot_metadata_provision.bin create-confirmed \
  --slot a \
  --image-version <initial-image-version> \
  --copy-a-output build/factory/boot_metadata_a.bin \
  --copy-b-output build/factory/boot_metadata_b.bin \
  --json-output build/factory/boot_metadata_provision.json
```

The complete mass-erase-to-first-boot workflow is documented in
`docs/factory_provisioning.md`.

## Reproducibility

Run:

```sh
python3 tools/check_deterministic_build.py
```

The deterministic check exports `HEAD` into two temporary source trees, builds
EXP045, EXP065, and EXP066, signs deterministic test images with a non-secret
test seed, generates release manifests and Slot A/Slot B update packages, and
compares the resulting ELF, BIN, HEX, signed image, update package, and release
JSON hashes.

Update-package verification reports are compared after normalizing only the
volatile `verification_timestamp_utc` field. Firmware binaries, signed packages,
package hashes, key fingerprints, slot metadata, and verification results remain
part of the deterministic comparison.

CI uses the same deterministic test seed only for reproducibility checks. It is
not a production signing key and is not a release trust anchor.

## Security Boundaries

This workflow improves supply-chain integrity and reproducibility evidence. The
repository contains a research UART update transport, but the release workflow
itself does not provide:

- hardware flashing or field deployment automation
- physical recovery input
- hardware-backed rollback counters
- option-byte provisioning
- RDP or WRP configuration
- production key custody, rotation, revocation, or HSM integration
- fault-injection, glitch, or side-channel resistance
