# Release Process

This document describes the current reproducible release workflow for the
laboratory secure-boot baseline. It does not add firmware update transport,
recovery mode, option-byte provisioning, RDP, WRP, or production key handling.

## Scope

The release workflow covers:

- EXP045 Stage-0 bootloader build artifacts
- EXP065/EXP066 application build artifacts
- signed-image construction
- authenticated Slot A/Slot B update-package construction
- offline signed-image verification
- offline update-package verification
- release manifest generation
- deterministic rebuild comparison

The workflow is host-only. It does not flash hardware and does not prove
hardware boot behavior.

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
  SIGNING_SEED=/path/to/release_signing_seed.bin \
  PUBLIC_KEY_HEADER=../exp045_bootloader_v2/src/firmware_public_key.h
```

This produces Slot A and Slot B ELF, BIN, HEX, map, update package, package
inspection JSON, and offline verification JSON under:

- `firmware/exp066_research_platform_core/build/slot_a`
- `firmware/exp066_research_platform_core/build/slot_b`

## Sign Firmware

The signing seed must be supplied explicitly:

```sh
make -C firmware/exp066_research_platform_core signed \
  SIGNING_SEED=/path/to/release_signing_seed.bin
```

The output signed image is:

```text
firmware/exp066_research_platform_core/build/exp066_research_platform_core_signed.bin
```

The signer rejects unsupported manifest versions, unsupported flags, nonzero
reserved fields, invalid vector tables, and payloads outside the supported
application region before writing the signed image.

## Verify Signed Image

Verify the signed image offline against the public key compiled into Stage 0:

```sh
make -C firmware/exp066_research_platform_core verify-signed \
  SIGNING_SEED=/path/to/release_signing_seed.bin \
  PUBLIC_KEY_HEADER=../exp045_bootloader_v2/src/firmware_public_key.h
```

This creates:

```text
firmware/exp066_research_platform_core/build/exp066_research_platform_core_verify.json
```

The verification report is machine-readable JSON. The command exits with:

- `0` when verification succeeds
- `1` when artifact or signature verification fails
- `2` for command-line usage errors from `argparse`

## Generate Release Manifest

Run complete artifact verification and release-manifest generation:

```sh
python3 tools/release_artifacts.py verify-release \
  --bootloader-elf firmware/exp045_bootloader_v2/build/exp045_bootloader_v2.elf \
  --bootloader-bin firmware/exp045_bootloader_v2/build/exp045_bootloader_v2.bin \
  --bootloader-hex firmware/exp045_bootloader_v2/build/exp045_bootloader_v2.hex \
  --application-elf firmware/exp066_research_platform_core/build/exp066_research_platform_core.elf \
  --application-bin firmware/exp066_research_platform_core/build/exp066_research_platform_core.bin \
  --application-hex firmware/exp066_research_platform_core/build/exp066_research_platform_core.hex \
  --signed-image firmware/exp066_research_platform_core/build/exp066_research_platform_core_signed.bin \
  --public-key-header firmware/exp045_bootloader_v2/src/firmware_public_key.h \
  --manifest-output firmware/exp066_research_platform_core/build/release_manifest.json \
  --report-output firmware/exp066_research_platform_core/build/release_verification.json \
  --application-name exp066_research_platform_core \
  --bootloader-version exp045 \
  --release-version <release-tag> \
  --require-clean \
  --require-exact-tag
```

For non-tagged CI or local dry runs, omit `--require-clean` and
`--require-exact-tag`. Official releases should use both.

The release manifest includes:

- Git commit and exact tag when available
- compiler identity
- raw Makefile build options
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

`tools/release_artifacts.py verify-signed` verifies only one signed image.
`tools/release_artifacts.py verify-release` verifies the complete release
artifact set.
`tools/update_package.py verify` verifies an authenticated update package for a
specific slot and target compatibility identifier.

Both commands parse the manifest using explicit little-endian fields, verify
the payload SHA-512, verify the detached Ed25519 signature over the manifest,
validate vector-table policy, and reject malformed or incomplete artifacts.

The verifier does not require target hardware.

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

CI uses the same deterministic test seed only for reproducibility checks. It is
not a production signing key and is not a release trust anchor.

## Security Boundaries

This workflow improves supply-chain integrity and reproducibility evidence. It
does not provide:

- firmware update transport
- physical recovery
- hardware-backed rollback counters
- option-byte provisioning
- RDP or WRP configuration
- production key custody, rotation, revocation, or HSM integration
- fault-injection, glitch, or side-channel resistance
