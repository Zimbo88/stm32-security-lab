# Secure Boot Validation

This document describes the current validation coverage for EXP045, EXP065,
and EXP066. It combines automated host checks with completed RDP0 hardware
validation evidence. No default host or CI command flashes hardware or changes
Option Bytes.

## Configuration

- Bootloader address: `0x08000000`
- Slot A signed-image address: `0x08020000`
- Slot A manifest address: `0x08020000`
- Slot A signature address: `0x08020060`
- Slot A application vector table: `0x08020200`
- Slot B signed-image address: `0x08080000`
- Slot B application vector table: `0x08080200`
- Update-slot header version: `2`
- Target compatibility: `0xf429ab01`
- Image type: application (`1`)
- Minimum image version: `2`
- Payload hash: SHA-512
- Signature algorithm: Ed25519
- Manifest flags allowed mask: `0x00000000`
- Manifest reserved0/reserved1: authenticated target compatibility and image
  type for update-slot packages

## Canonical Manifest Requirements

The metadata-driven Stage-0 slot policy accepts only a canonical update-slot
manifest:

- magic equals `0x31474953`
- header version equals `2`
- image version is at least `2`
- vector address equals the selected slot payload base
- image size is at least 8 bytes and no larger than the selected slot payload
  capacity
- payload address arithmetic does not overflow
- complete payload remains inside the selected slot
- flags contain no unsupported bits
- target compatibility equals `0xf429ab01`
- image type equals application (`1`)
- payload hash matches the application bytes
- Ed25519 signature validates over the serialized manifest bytes

The initial MSP must be in the supported main SRAM range and 8-byte aligned.
The reset vector must have the Thumb bit set and resolve inside the accepted
payload.

After `signed_image_verify()` accepts an image, Stage 0 performs a second
manifest/payload/vector validation pass in `signed_image_prepare_jump()`.
The target jump path then validates the prepared context and re-reads the flash
vector table before updating `VTOR`, loading `MSP`, and branching to the
application.

## Automated Host Tests

Run:

```sh
PYTHONDONTWRITEBYTECODE=1 pytest -q -p no:cacheprovider tests
make -C tests/host_verifier clean test
make -C tests/host_verifier clean test SANITIZE=1
python3 tools/check_deterministic_build.py
```

The host C verifier tests compile the production `signed_image.c` verifier
with Monocypher and strict host warnings. They cover:

- valid image
- null verifier inputs
- minimum payload length
- maximum payload length
- payload capacity one byte short
- payload one byte too large
- final jump-context revalidation in host mode
- bad magic
- unsupported header version
- invalid payload length
- bad manifest vector address
- address overflow
- bad MSP
- lowest accepted MSP
- highest accepted MSP
- MSP above supported SRAM
- unaligned MSP
- bad reset vector
- reset vector before payload
- reset vector outside payload
- reset vector at flash end
- reset vector at the last accepted payload address
- modified payload
- modified signature
- unsupported flags
- unsupported high flag bit
- noncanonical reserved fields
- noncanonical second reserved field
- rollback rejection
- stable verifier status-code values

The Python signer tests cover payload size limits, malformed vector tables,
unsupported flags/reserved fields through the signer API, truncated payloads,
deterministic output, and atomic-output failure behavior.

The release artifact tests cover:

- offline signed-image verification
- release-manifest generation
- modified payload artifacts
- invalid signatures
- unsupported manifest versions
- invalid payload hashes
- missing release artifacts
- inconsistent expected application versions
- deterministic-output mismatch reporting

The deterministic build check compares ELF, BIN, HEX, update packages, package
inspection JSON, and package verification JSON outputs from two independent
archived source trees.

For update-package verification JSON, only the volatile
`verification_timestamp_utc` field is normalized before comparison. The
remaining report fields, including package hashes, key fingerprints, slot
metadata, and verification booleans, are still compared.

## What Host Tests Prove

The host tests provide repeatable evidence for manifest parsing, little-endian
field decoding, policy checks, SHA-512 digest comparison, Ed25519 signature
verification, signing-tool input validation, boundary-value rejection, redundant
jump-context validation in host mode, release artifact integrity, reproducible
release metadata, and failure-code stability.

## Hardware Validation Evidence

The secure-update-v2 completion run on 2026-07-27 validated the boot and update
chain on an STM32F429IGT6-class target at RDP Level 0. The campaign observed:

- Slot A confirmed boot with a concrete slot decision;
- SHA-512 and Ed25519 execution before application jump;
- valid A-to-B update, Slot B trial boot, and Slot B confirmation;
- valid B-to-A update, Slot A trial boot, and Slot A confirmation;
- rollback rejection for same and lower versions;
- fail-closed rejection of corrupted manifest, target, signature, and payload
  cases;
- fallback after abort and reset during `WRITING`;
- UART CRC, sequence, partial-frame, and random-byte negative cases;
- flash readback matching the transferred packages after positive updates;
- Option Bytes unchanged throughout the campaign.

See `docs/validation-summary.md` and `docs/release-readiness.md`.

## What Host Tests Do Not Prove

The host tests do not validate:

- flash programming or erase behavior
- hardware reset sequencing
- VTOR relocation on silicon
- interrupt behavior after the jump
- option bytes, WRP, RDP, or debug locking
- GitHub release settings or tag protection rules
- power-loss recovery
- physical recovery entry
- fault-injection or glitch resistance
- side-channel resistance
- production key custody or provisioning

Those properties require dedicated hardware validation and external security
review before any production claim.

## Secure Failure Behavior

On a verification failure, the bootloader prints the failure status and halts
or falls back through the documented slot policy instead of starting the
rejected image. A physical recovery input is not selected in this baseline, so
fail-closed halt remains safe but not operationally complete when no confirmed
fallback image is available.

The jump path also treats final handoff validation failures as security
failures: it returns an explicit verifier status to the boot sequence, which
prints the status and enters the centralized halt path.

## Remaining Security Boundaries

The current implementation includes a research UART secure-update transport,
but it still lacks hardware-backed rollback state, physical recovery,
bootloader write protection, debug/Option-Byte provisioning policy, production
key custody, and fault-injection countermeasures. It must not be described as
production ready.
