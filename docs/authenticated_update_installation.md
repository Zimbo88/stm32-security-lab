# Authenticated Update Installation

This phase adds a host-simulated authenticated firmware update installer. The
follow-on Stage-0 slot-selection, trial boot, confirmation, and fallback phase
is documented in `docs/stage0_slot_selection.md`. This installer phase does not
add real target flash programming, transport, option-byte programming, WRP, RDP,
or physical hardware flashing.

## Package Format

An update package is the existing signed-image container with manifest version
`2`. There is no second signature format.

```text
offset  size  field
0x000   0x60  signed manifest
0x060   0x40  Ed25519 signature over the manifest
0x0a0   0x160 padding, all 0xff
0x200   n     application payload
```

The package length must be exactly `0x200 + image_size`. Truncated packages,
oversized packages, non-`0xff` padding, and trailing data are rejected.

## Authenticated Fields

The Ed25519 signature covers the 96-byte little-endian manifest. For update
package version `2`, the manifest fields are:

- `magic`: `SIGNED_IMAGE_MAGIC`
- `header_version`: `UPDATE_PACKAGE_FORMAT_VERSION`
- `image_version`: candidate firmware version
- `vector_address`: payload base of the target slot
- `image_size`: payload byte length
- `flags`: supported flags, currently zero
- `reserved0`: `UPDATE_PACKAGE_TARGET_STM32F429IGT6_AB_V1`
- `reserved1`: `UPDATE_PACKAGE_IMAGE_TYPE_APPLICATION`
- `payload_sha512`: SHA-512 of the payload

The v2 profile reuses the existing manifest storage for target compatibility
and image type. Legacy signed images remain version `1` and still require both
reserved fields to be zero. The installer accepts only v2 packages.

## Security Boundary

The trust boundary is the existing production Ed25519/SHA-512 verifier and the
trusted public key compiled into Stage 0. The installer uses a slot-aware entry
point in the same verifier so that the signature, payload hash, target
compatibility, image type, vector table, MSP, reset handler, execution bounds,
flags, reserved fields, and rollback floor are checked before installation.

Metadata CRCs are not authenticity protection. They detect accidental
corruption and support deterministic recovery. Until WRP, RDP, option bytes, or
hardware rollback counters are added, metadata-backed rollback state depends on
Stage 0 and the simulated installer being the only trusted metadata writers.

## Install Sequence

The host-simulated installer performs this sequence:

1. Recover validated metadata.
2. Require a confirmed active slot.
3. Select the opposite inactive slot.
4. Parse the complete package and verify it for that inactive slot.
5. Reject candidates that do not advance beyond the confirmed metadata version.
6. Create a restricted flash view for metadata copies and the inactive slot.
7. Commit metadata state `WRITING`.
8. Erase every sector in the inactive slot.
9. Program aligned bounded chunks through `boot_flash`.
10. Read back and compare every programmed chunk.
11. Hash the complete installed payload from flash.
12. Re-verify the installed bytes through the production signed-image verifier.
13. Commit metadata state `CANDIDATE_READY`.

The installer does not transition to pending trial boot.

## Failure Handling

Every installer failure returns an explicit status. Tests assert that failures
leave Stage 0, the confirmed active slot, and recovery byte-for-byte unchanged;
do not touch the update-metadata sector; preserve deterministic metadata
recovery; and never mark a partial candidate as ready.

Deterministic host fault injection covers:

- metadata `WRITING` transition
- every inactive-slot sector erase
- first, middle, and final program blocks
- flash read-back
- complete installed-payload hash
- installed-image verifier invocation
- `CANDIDATE_READY` metadata transition
- metadata commit-marker write

## Host Tooling

Build a package for a slot-linked payload:

```sh
python3 tools/update_package.py build \
  --application path/to/app_for_slot_b.bin \
  --seed path/to/test_or_release_seed.bin \
  --slot b \
  --image-version 3 \
  --output build/update.supkg
```

Inspect metadata:

```sh
python3 tools/update_package.py inspect --package build/update.supkg
```

Verify offline:

```sh
python3 tools/update_package.py verify \
  --package build/update.supkg \
  --slot b \
  --public-key-header firmware/exp045_bootloader_v2/src/firmware_public_key.h
```

Install into deterministic simulated flash:

```sh
python3 tools/update_package.py install-sim \
  --package build/update.supkg \
  --active-slot a \
  --active-version 2 \
  --public-key-header firmware/exp045_bootloader_v2/src/firmware_public_key.h \
  --flash-output build/simulated_flash.bin
```

Default commands and CI jobs are host-only and do not flash hardware.

## Deferred Work

Later phases must add hardware-backed rollback protection, target
flash-controller programming, transport, USB DFU, UART update handling,
WRP/RDP/option-byte provisioning, and physical hardware validation.
