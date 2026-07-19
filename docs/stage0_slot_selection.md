# Stage-0 Slot Selection And Trial Boot

This phase adds deterministic Stage-0 slot selection, pending-trial boot
attempt accounting, application confirmation, fallback to the last confirmed
slot, and host simulation. It does not add UART transport, USB DFU, networking,
option-byte programming, WRP, RDP, production flash programming,
hardware-backed rollback protection, irreversible provisioning, or hardware
flashing.

## Security Boundary

Stage 0 still trusts only the compiled Ed25519 public key and the production
signed-image verifier. Slot selection never treats metadata CRCs as
authenticity. Metadata only selects which already-authenticated slot may be
verified and booted.

Every bootable image is verified before use. The verifier checks the canonical
v2 signed-image manifest, signature, payload SHA-512, target compatibility,
image type, flags, vector table, MSP, reset handler, and slot execution bounds.
The jump context is revalidated against immutable slot descriptors immediately
before control transfer.

## Metadata States

The installer ends in `CANDIDATE_READY`. Stage 0 owns the transition from
candidate-ready to trial boot:

```text
CONFIRMED -> WRITING -> CANDIDATE_READY
CANDIDATE_READY -> PENDING_TRIAL
PENDING_TRIAL -> PENDING_TRIAL
PENDING_TRIAL -> CONFIRMED
PENDING_TRIAL -> REJECTED_INVALID
```

`PENDING_TRIAL -> PENDING_TRIAL` is allowed only when it preserves the same
active slot, candidate slot, image version, and decrements the remaining attempt
count by exactly one. A pending record with zero attempts is canonical and means
the candidate has exhausted its trial budget.

## Slot Selection Policy

On each boot, Stage 0:

1. Recovers redundant metadata.
2. Fails closed on ambiguous metadata or no recoverable valid copy.
3. Boots a `CONFIRMED` active slot only after verification.
4. Falls back to the last confirmed active slot for `WRITING` and
   `REJECTED_INVALID`.
5. For `CANDIDATE_READY`, verifies the candidate, atomically commits
   `PENDING_TRIAL` with one attempt consumed, then boots the candidate.
6. For `PENDING_TRIAL`, falls back if attempts are exhausted; otherwise verifies
   the candidate, atomically decrements attempts, then boots the candidate.
7. If candidate verification fails, records `REJECTED_INVALID` on a best-effort
   basis and falls back to the confirmed active slot.

If an attempt-decrement commit fails, the candidate is not booted. Stage 0
verifies and boots the last confirmed slot when available. This avoids trial
boots that were not durably accounted for.

## Application Confirmation

`boot_confirm_current_slot()` is the narrow confirmation API. The running
application should call it only after reaching its documented healthy state.

Confirmation is idempotent. It confirms only when metadata is `PENDING_TRIAL`
and the supplied running slot matches the metadata candidate slot. It rejects
attempts to confirm the last confirmed slot, another candidate, invalid slot
ids, ambiguous metadata, and non-pending states. A repeated call after the slot
is already confirmed returns success without writing a new record.

## Fallback Behavior

Stage 0 returns to the last confirmed slot when:

- a candidate does not verify
- no confirmation arrives before attempts reach zero
- a trial attempt cannot be atomically recorded
- metadata recovers to `WRITING` or `REJECTED_INVALID`
- one metadata copy is torn or corrupt but the other copy is valid

Stage 0 fails closed instead of guessing when both metadata copies are invalid,
metadata copies are ambiguous, the confirmed slot id is invalid, or the
confirmed image does not verify.

## Host Simulation And Fault Injection

`tests/update_storage` simulates the full lifecycle with deterministic flash:

- successful upgrade and confirmation
- failed upgrade with invalid candidate fallback
- missing confirmation across repeated resets
- attempt exhaustion
- metadata copy corruption and deterministic recovery
- power loss during pending-trial commit
- ambiguous metadata
- invalid slot metadata
- invalid images
- confirmation of the wrong slot
- confirmation commit failure

The simulator also asserts that selection and confirmation update only metadata
through the metadata API. No host or CI command performs hardware flashing.

## Hardware Limitations

The target flash backend remains write-disabled. Stage 0 can read memory-mapped
flash through a read-only target backend, but production erase/program support
is intentionally deferred. Trial boot and confirmation that require metadata
writes must be validated on hardware only after an explicitly reviewed target
flash backend, WRP/RDP/option-byte policy, and provisioning process exist.
