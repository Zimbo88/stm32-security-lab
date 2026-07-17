# Update Storage Foundation

This document covers only the storage and metadata foundation for a later
authenticated firmware update flow. It does not implement package installation,
Stage-0 slot selection, trial boot, application confirmation, fallback policy,
transport, option-byte programming, WRP, RDP, or hardware flash programming.

## Storage Model

The update-storage foundation uses the authoritative dual-slot layout in
`config/stm32f429_memory_layout.json`. Stage 0 is confined to S0-S1. Metadata
copy A and copy B use separate 16 KiB sectors. Slot A and Slot B are equal
896 KiB signed-image regions, each with a 512-byte canonical signed-image
header followed by the application payload. The recovery region is outside both
slots.

The current application and signer compatibility constants still alias Slot A:

- signed-image base: `0x08020000`
- application vector base: `0x08020200`
- maximum payload: `0x000dfe00` bytes

Slot B constants are generated now for later update policy:

- signed-image base: `0x08100000`
- application vector base: `0x08100200`
- maximum payload: `0x000dfe00` bytes

## Slot Descriptors

`boot_slot_lookup()` returns immutable descriptors derived from generated
layout constants. Each descriptor includes the slot id, signed-image base,
manifest address, signature address, payload base, slot end, maximum payload
size, first sector, and last sector. Invalid slot ids return an explicit invalid
result and no descriptor.

No update policy should duplicate raw slot addresses; it should use these
descriptors.

## Flash Abstraction

`boot_flash` is a narrow target-neutral abstraction with:

- bounded read
- sector erase
- aligned programming
- read-back verification after programming
- generated sector lookup
- explicit status codes
- caller-supplied write-region policy

All address, length, alignment, overflow, and write-region checks happen before
the backend erase/program call. The host simulator enforces flash programming
semantics by allowing only 1-to-0 bit transitions. Rejected Stage-0, active-slot,
and recovery-region operations do not reach the backend in the tests.

`boot_flash_target_init_disabled()` is a fail-closed target stub. It performs no
register access and exposes no writable regions. Reads return a backend error;
erase and program operations are rejected as protected. A later hardware phase
must replace this with an explicitly reviewed target backend.

## Metadata Format

Each metadata copy stores one canonical 128-byte little-endian record. The
commit marker is the final 8 bytes and is written after the record body.

| Offset | Size | Field |
|---:|---:|---|
| `0x00` | 4 | magic |
| `0x04` | 4 | format version |
| `0x08` | 4 | record size |
| `0x0c` | 4 | sequence |
| `0x10` | 4 | state |
| `0x14` | 4 | active slot |
| `0x18` | 4 | candidate slot |
| `0x1c` | 4 | candidate image version |
| `0x20` | 4 | boot-attempt count |
| `0x24` | 4 | confirmation state |
| `0x28` | 4 | result code |
| `0x2c` | 16 | reserved, must be zero |
| `0x3c` | 4 | CRC32 over bytes `0x00-0x3b` |
| `0x40` | 56 | padding, must be zero |
| `0x78` | 8 | commit marker |

The CRC detects accidental corruption only. It is not authenticity protection.
Metadata authenticity in this phase depends on Stage 0 being the only trusted
writer of the metadata sectors; that assumption is not yet hardware-enforced
with WRP or option bytes.

## Metadata States

The storage layer defines these states for later update policy:

- `EMPTY`
- `WRITING`
- `CANDIDATE_READY`
- `PENDING_TRIAL`
- `CONFIRMED`
- `REJECTED_INVALID`

Allowed transitions are explicit:

```text
EMPTY -> WRITING
EMPTY -> CONFIRMED
CONFIRMED -> WRITING
CONFIRMED -> CONFIRMED
WRITING -> CANDIDATE_READY
WRITING -> REJECTED_INVALID
CANDIDATE_READY -> PENDING_TRIAL
CANDIDATE_READY -> REJECTED_INVALID
PENDING_TRIAL -> CONFIRMED
PENDING_TRIAL -> REJECTED_INVALID
REJECTED_INVALID -> WRITING
```

The code rejects unknown states, invalid slots, non-zero reserved fields, stale
or wrapped sequences, invalid boot-attempt counts, and conflicting equal
sequence records. Sequence `0` is reserved for the in-RAM empty baseline and is
not encoded as a committed flash record. Sequence `UINT32_MAX` is fail-closed.

## Recovery Rules

Recovery decodes both copies independently. If exactly one copy is valid, that
copy is selected. If both are valid and have different sequence numbers, the
higher sequence is selected. If both are valid with the same sequence but
different fields, recovery fails as ambiguous. If neither copy is valid, recovery
returns an empty in-RAM baseline and reports no valid copy.

A metadata update writes the alternate or missing copy. The target sector is
erased, the body is programmed and verified, then the commit marker is
programmed and verified. Records without the final commit marker are never
selected.

## Host Simulation And Tests

`tests/update_storage` builds the storage layer against a deterministic host
flash simulator. The simulator models the full 2 MiB STM32F429 flash array,
sector erase, 1-to-0 programming, read-back corruption, and operation-count
failure injection.

The host tests cover:

- generated sector lookup and bounds
- protected Stage-0, active-slot, and recovery-region rejection
- disabled target backend behavior
- alignment and address-overflow rejection before backend calls
- read-back verification failure
- metadata encode/decode/recovery
- invalid state, slot, reserved, padding, commit-marker, and truncated records
- deterministic equal-sequence handling
- conflicting equal-sequence rejection
- sequence exhaustion
- state-transition validation
- uncommitted metadata record rejection
- injected failures before and after the metadata commit marker
- sanitized host execution when the compiler supports it

The tests do not flash hardware and do not validate STM32 flash-controller
register sequencing.

## Remaining Limitations

This phase is not a complete updater. The following remain for later phases:

- authenticated update package parsing and signature verification
- installing images into the inactive slot
- Stage-0 slot selection
- trial boot and boot-attempt accounting in Stage 0
- application confirmation API
- automatic fallback
- rollback-floor persistence
- transport and recovery policy
- target flash-controller backend
- hardware validation of erase/program timing and power-loss behavior
- hardware-backed metadata protection or anti-rollback
