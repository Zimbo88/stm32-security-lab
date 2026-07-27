# Secure Update Chain Audit

Audit date: 2026-07-23. Hardware completion evidence was added on 2026-07-27.

## Scope

Reviewed end-to-end path:

```text
stm32ctl
-> UART framing
-> protocol parser
-> update service
-> streaming installer
-> flash backend
-> boot metadata
-> candidate verification
-> candidate boot
-> confirmation / rollback
```

The review focused on memory safety, integer overflows, invalid state
transitions, parser desynchronization, replay and duplicate handling, retry
semantics, unauthorized flash access, rollback protection, candidate release,
power-loss behavior, stack usage, bootloader size, and undocumented
assumptions.

## Summary

One concrete correctness issue was found and fixed during the audit:

- When `FINISH_UPDATE` had successfully completed installation and committed
  `CANDIDATE_READY`, an I/O failure while sending the final ACK/status frame
  could make `update_service_run()` return to the normal boot path instead of
  forcing the promised controlled reset.
- The candidate was already verified and still had to pass boot-time
  verification before execution, so this was not an integrity bypass.
- The update-service contract was tightened so a `FINISHED` or
  `RESET_REQUESTED` protocol state requests reset even after a writer/I/O
  error.
- A regression test simulates a final `FINISH_UPDATE` ACK writer failure and
  checks that `CANDIDATE_READY` remains committed and reset is requested.

No proven path was found to overwrite the active slot, bootloader region, or
reserved recovery region through the update path.

## Reviewed Behavior

### stm32ctl

- Frames are little-endian, bounded to 1024 payload bytes, and protected by
  CRC32.
- The host tool validates local update packages through the existing
  `tools/update_package.py` path and the configured public key.
- The tool does not contain private signing material and never signs firmware.
- Non-idempotent update commands are not retried automatically. A lost
  `WRITE_BLOCK` ACK fails closed instead of replaying a state-changing block.
- Lost ACKs for idempotent commands can surface as timeout or sequence errors.
  This is an availability limit, not a proven integrity issue.

### UART Framing And Parser

- The parser is incremental and resynchronizes only on `SUPD`.
- Oversize payloads are rejected before writing to the fixed frame buffer.
- CRC, protocol version, and payload length are checked before command
  execution.
- Frames with bad version, bad CRC, or oversize payload do not consume a
  sequence number.
- Validly decoded but logically rejected commands consume the expected sequence
  number.
- Tests cover byte-by-byte input, fragmented frames, multiple frames, garbage
  before magic, wrong version, oversize payloads, CRC errors, unknown commands,
  duplicate and skipped sequences, mid-frame timeout, and random input.

### Update Service

- The entry window is bounded by poll and byte budgets. UART noise cannot block
  normal boot forever.
- Only a valid binary `HELLO` with sequence 0 enters update mode.
- A failed or incomplete update is aborted when a safe abort is still possible.
- A completed update requests controlled reset even when the final response
  frame cannot be transmitted successfully.

### Streaming Installer

- The complete package never has to reside in RAM.
- Header, manifest, signature, target, slot, padding, size, and rollback policy
  are checked before candidate erase.
- The installer determines the inactive slot from confirmed metadata and
  rejects packages for the active slot.
- Rollback protection runs before `WRITING` and before erase:
  `manifest.image_version` must be greater than the confirmed metadata version.
- Payload offsets must be exactly monotonic. Skipped, duplicate, overlapping,
  or extra bytes put the session into a failure state.
- `CANDIDATE_READY` is committed only after streaming SHA-512 comparison,
  flash-readback hashing, installed-image verification, and header/padding
  checks.
- `signed_image_verify_update_slot_buffer()` treats the supplied capacity only
  as an upper bound and hashes exactly `manifest.image_size`.

### Flash Backend And Metadata

- The installer uses a restricted flash instance that exposes only metadata A,
  metadata B, and the inactive candidate slot as writable regions.
- `boot_flash_program_aligned()` checks address, length, alignment, write
  region, overflow, and readback before reporting success.
- Metadata uses two copies, CRC, and a commit marker. Torn records without a
  valid commit marker are never selected.
- `WRITING` and `REJECTED_INVALID` do not boot the candidate. After reset
  during begin, erase, write, or finish, slot selection falls back to the
  confirmed active slot when it verifies.

### Candidate Boot, Confirmation, And Rollback

- `CANDIDATE_READY` is verified again before `PENDING_TRIAL` is committed.
- Trial boots decrement the attempt counter.
- Without application confirmation, attempts eventually exhaust and Stage 0
  falls back to the last confirmed slot.
- Application confirmation is valid only from `PENDING_TRIAL` for the running
  candidate slot and then promotes that slot to `CONFIRMED`.

## Finding Fixed

### Final ACK I/O Error After Successful Update

Risk: after successful `update_installer_finish()`, `CANDIDATE_READY` was
already committed. If sending the final ACK/status frame failed,
`update_service_run()` treated the error like a generic protocol failure and
returned to the normal boot path.

Impact: flash integrity was not violated, because the candidate still required
boot-time verification. The behavior nevertheless violated the update-service
contract that a successful update ends in a controlled reset.

Correction:

- `firmware/exp045_bootloader_v2/src/update_service.c`: if
  `protocol_status != OK`, the service now checks `FINISHED` and
  `RESET_REQUESTED` first and requests reset for those states.
- `tests/update_protocol/test_update_protocol.c`: added
  `test_update_service_final_ack_io_error_still_resets()`.

## Hardware Root Causes Later Confirmed

The 2026-07-27 RDP0 hardware run later found two timing/integration issues and
validated their fixes:

- `BEGIN_UPDATE` ACK took about 6.3 seconds because the target erases the
  inactive slot before acknowledging begin. The previous 1 second host timeout
  was too short for real hardware; the documented and default host timeout is
  now 15 seconds.
- Reset immediately after `FINISH_UPDATE` could truncate the final ACK on
  hardware. The target now waits for USART transmission complete before
  requesting reset.
- Trial boot initially failed because boot policy used a read-only flash
  backend for the `CANDIDATE_READY -> PENDING_TRIAL` commit. Boot policy now
  uses the reviewed metadata-only target flash backend for that transition.

## Remaining Assumptions And Limits

- UART update frames are protected against accidental corruption and parser
  confusion, but the session is not operator-authenticated. Authorization comes
  from the Ed25519 package signature and rollback policy.
- Internal flash must remain consistently memory-mapped while hashing,
  verifying, and preparing the jump. The target backend handles cache behavior
  around flash operations; there is no separate full re-hash immediately before
  `signed_image_jump()`.
- After slot selection, the jump path validates the prepared jump context
  against the flash vector table. It assumes no further bootloader flash writes
  occur in that phase.
- Lost ACKs for idempotent host commands can become visible sequence NACKs. The
  host fails rather than trying side-effecting resynchronization.
- No physical update GPIO is selected; update entry uses the bounded binary
  `HELLO` window.
- No dedicated AFL, Hypothesis, or QuickCheck fuzz target was found. The
  repository has deterministic parser and random-input tests.

## Verification Run

The audit verification run completed successfully:

```text
make -C tests/update_protocol clean test SANITIZE=1
make -C tests/update_storage clean test SANITIZE=1
make -C tests/uart clean test SANITIZE=1
make -C tests/diagnostic_console clean test SANITIZE=1
make -C tests/host_verifier clean test SANITIZE=1
make -C tools clean test SANITIZE=1
make -C tests/rsm_core clean test
python3 -m pytest
.venv-hil/bin/ruff check .
.venv-hil/bin/mypy .
cd tools/secure_boot_hil && ../../.venv-hil/bin/ruff check .
cd tools/secure_boot_hil && ../../.venv-hil/bin/mypy secure_boot_hil host_tests
make -C firmware/exp045_bootloader_v2 clean report
bash audit/run_repository_audit.sh
git diff --check
rg -n "installed_image_buffer|installed_image_buffer_size" . -S
```

Observed results:

- Pytest: `157 passed in 39.27s` at the audit point.
- Ruff: all checks passed.
- Mypy: no issues found in 79 source files.
- Bootloader binary size at the audit point: `29,824 / 32,768` bytes.
- `installed_image_buffer` repository search: no references.

The current release-readiness results are tracked in
`docs/secure-update-release-candidate.md` and `docs/validation-summary.md`.
