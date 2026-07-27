# Secure Update Streaming Installer Design

Status: implemented architecture.

This document describes the stateful streaming API that extends the existing
update installer while preserving the compatibility `update_installer_install()`
entry point. The signed-image and update-package format is unchanged.

## Goals

- The complete update package never has to reside in RAM.
- No dynamic memory allocation is used.
- The caller provides small fixed program and readback buffers.
- The package format remains:
  `manifest || signature || header-padding || payload`.
- Manifest and signature are validated before the candidate slot is erased.
- Ed25519 verification remains mandatory.
- Rollback protection runs before the first candidate-slot write.
- The inactive slot is determined from boot metadata, not from a host address.
- Metadata states `WRITING` and `CANDIDATE_READY` remain the durable update
  boundary.
- Reset or power loss during begin, erase, write, or finish must never make a
  partially written candidate bootable.
- After writing, the installed image is still read back from flash and verified.

## Existing Compatibility API

`update_installer_install()` remains available for callers that already hold a
complete package buffer. It is implemented as a wrapper around the same
session lifecycle:

```text
session_init -> begin(header) -> write(payload blocks) -> finish
```

The wrapper must not weaken state checks, rollback checks, installed-image
verification, fault injection, or readback behavior.

## Public API Shape

The implemented API follows the existing `update_installer_*` naming style:

```c
typedef enum {
    UPDATE_INSTALL_SESSION_EMPTY = 0,
    UPDATE_INSTALL_SESSION_INITIALIZED,
    UPDATE_INSTALL_SESSION_WRITING,
    UPDATE_INSTALL_SESSION_PAYLOAD_COMPLETE,
    UPDATE_INSTALL_SESSION_FINISHED,
    UPDATE_INSTALL_SESSION_ABORTED,
    UPDATE_INSTALL_SESSION_FAILED
} update_installer_session_state_t;

typedef struct update_installer_session update_installer_session_t;

update_install_status_t update_installer_session_init(
    update_installer_session_t *session,
    const boot_flash_t *flash,
    const uint8_t public_key[FIRMWARE_PUBLIC_KEY_SIZE],
    const update_install_options_t *options,
    update_install_result_t *result
);

update_install_status_t update_installer_begin(
    update_installer_session_t *session,
    const uint8_t header[SIGNED_IMAGE_HEADER_SIZE],
    size_t header_size
);

update_install_status_t update_installer_write(
    update_installer_session_t *session,
    size_t payload_offset,
    const uint8_t *data,
    size_t length
);

update_install_status_t update_installer_finish(
    update_installer_session_t *session
);

update_install_status_t update_installer_abort(
    update_installer_session_t *session
);
```

The session object is caller allocated. Large I/O buffers remain in
`update_install_options_t`; no full package buffer is part of the session.

## Session State

Important session fields:

- selected active slot;
- selected inactive candidate slot;
- metadata recovered before the update;
- restricted flash instance with writable metadata and candidate regions only;
- parsed manifest, raw manifest bytes, and raw signature bytes;
- expected payload size and total package size;
- streaming SHA-512 context;
- accepted payload offset;
- programmed block count and final result fields;
- boolean markers for metadata `WRITING`, candidate erase, payload completion,
  and final verification.

The installer starts only from a confirmed active metadata state. Existing
`CANDIDATE_READY` and `PENDING_TRIAL` states must be handled by the boot/trial
flow before a new update is accepted.

## Header Verification

`update_installer_begin()` accepts exactly `SIGNED_IMAGE_HEADER_SIZE` bytes.
Header-only verification checks:

- header size equals 512 bytes;
- manifest decodes using explicit little-endian fields;
- magic and header version are supported;
- flags are supported and canonical;
- target compatibility is `UPDATE_PACKAGE_TARGET_STM32F429IGT6_AB_V1`;
- image type is application;
- image size is within the selected candidate slot capacity;
- `SIGNED_IMAGE_HEADER_SIZE + image_size` does not overflow;
- vector address equals the candidate slot payload base;
- header padding between signature and payload is all `0xff`;
- Ed25519 signature validates over the serialized 96-byte manifest.

At this point the payload is not yet available, so payload SHA-512 and vector
table contents cannot be checked. Safety comes from authenticating the manifest,
checking rollback before erase, committing `WRITING`, and requiring final flash
verification before `CANDIDATE_READY`.

## Begin Sequence

`update_installer_begin()` performs:

1. Header-only verification for the inactive candidate slot.
2. Rollback check against the confirmed active image version.
3. Metadata commit to `WRITING`.
4. Recovery check that `WRITING` is visible.
5. Candidate-slot erase through the restricted flash instance.
6. Header programming and readback.
7. SHA-512 context initialization for streaming payload bytes.
8. Transition to `UPDATE_INSTALL_SESSION_WRITING`.

If power fails before the `WRITING` commit, old metadata remains selected. If
power fails after the `WRITING` commit, boot policy falls back to the confirmed
active slot and never boots the partially written candidate.

## Write Sequence

`update_installer_write()` accepts payload bytes only. Header bytes are never
processed by `write()`.

Rules:

- session state must be `WRITING`;
- `data` must be non-null when `length > 0`;
- `length` must be greater than zero;
- `payload_offset` must equal the number of payload bytes already accepted;
- `payload_offset + length` must not overflow;
- accepted bytes must not exceed the manifest `image_size`;
- the block is accepted completely or not at all.

Duplicate, skipped, overlapping, out-of-order, and extra blocks are rejected
with sequence/state/package errors and do not mark the candidate ready.

Each accepted byte range is fed into the SHA-512 context, programmed through
the restricted flash backend, and read back through the caller-provided
readback buffer.

## Finish Sequence

`update_installer_finish()` requires that exactly `manifest.image_size` payload
bytes have been accepted.

The finish step:

1. finalizes the streaming SHA-512 context;
2. compares the digest with the authenticated manifest digest;
3. reads the installed payload from flash and hashes it again;
4. verifies the installed image directly from the memory-mapped candidate slot
   with `signed_image_verify_update_slot_buffer()`;
5. checks the installed header and padding;
6. commits `CANDIDATE_READY`;
7. records the installed image version and programmed block count;
8. transitions to `FINISHED`.

`signed_image_verify_update_slot_buffer()` receives the candidate manifest
address, signature address, payload base, and maximum payload capacity. The
capacity is an upper bound only. The verifier hashes exactly
`manifest.image_size`.

## Abort And Reset Behavior

`update_installer_abort()` is a best-effort local session cleanup:

- before `begin`, it leaves metadata unchanged;
- after `WRITING`, it does not mark the candidate ready;
- after a session failure, it leaves the boot policy to recover the last
  confirmed slot;
- after `FINISHED`, abort is not used to undo `CANDIDATE_READY`.

After reset:

- `WRITING` is non-bootable and falls back to the confirmed slot;
- `CANDIDATE_READY` is verified again before trial boot;
- `PENDING_TRIAL` consumes attempts and requires application confirmation;
- an unconfirmed candidate eventually falls back to the confirmed slot.

## Integer And Bounds Rules

All size and address arithmetic must be checked before casting to narrower
types:

- package size = `SIGNED_IMAGE_HEADER_SIZE + manifest.image_size`;
- payload end = `candidate->payload_base + manifest.image_size`;
- flash program ranges must remain inside `candidate->maximum_payload_size`;
- program and readback buffers must be non-null and large enough for the chosen
  chunk size;
- address alignment is enforced by `boot_flash_program_aligned()`.

The caller-provided maximum payload capacity must never be interpreted as the
actual payload length.

## Fault Injection

Existing fault-injection points remain attached to the same security-relevant
operations:

- metadata `WRITING` commit;
- candidate-sector erase;
- header and payload programming;
- readback;
- installed payload hash;
- installed-image verification;
- `CANDIDATE_READY` commit;
- metadata commit marker write.

Fault-injection tests must prove that failures before final commit do not make
the candidate bootable.

## Test Expectations

Required regression coverage includes:

- valid packages in 1-byte, 64-byte, 512-byte, and 1024-byte transfer blocks;
- package larger than program/readback buffers;
- truncated header;
- bad padding;
- bad signature;
- bad hash;
- same and lower versions;
- wrong slot;
- wrong block order;
- duplicate block;
- too much data;
- finish before complete transfer;
- abort before and after begin;
- reset/power-failure simulation around metadata, erase, write, finish, and
  candidate-ready;
- A-to-B and B-to-A updates;
- fault injection at every supported point.

## Security Limitations

- Payload hash cannot be fully checked before candidate erase because payload
  bytes arrive later.
- The streaming API is not a resume-after-reset protocol. A reset during
  transfer requires a new update attempt.
- `WRITING` and `REJECTED_INVALID` cleanup policy is deliberately outside the
  installer session; boot policy keeps those states non-bootable.
- Final verification assumes the candidate slot is readable through
  memory-mapped internal flash.
- UART session authorization is the signed firmware package and rollback
  policy, not a separate host identity.
