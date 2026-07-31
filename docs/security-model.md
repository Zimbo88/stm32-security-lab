# Security model

Status vocabulary: `DESIGNED`, `IMPLEMENTED`, `HOST TESTED`,
`HARDWARE VALIDATED`, `DOCUMENTED ONLY`, and `NOT IMPLEMENTED` are kept
separate. Hardware validation means a recorded test on the STM32F429 board;
host tests are not hardware evidence.

## Security objectives

The platform is intended to provide these properties at RDP Level 0 and, after
a separate manual decision, as the software path for an RDP2 test device:

1. Only an image with the expected manifest, SHA-512 payload hash, Ed25519
   signature, vector table, address range, and version may be booted.
2. Only the inactive slot may be erased and programmed by the UART installer.
3. Stage 0, the public key, metadata sectors, and the recovery sector are not
   update targets.
4. A candidate must pass trial boot and the application health gate before it
   becomes confirmed. A failed trial returns to the last confirmed slot.
5. Invalid or ambiguous metadata fails closed into signed UART recovery.
6. Normal recovery cannot bypass signature, target, bounds, hash, or version
   checks.

## Root of trust

The trust anchor is the 32-byte Ed25519 public key compiled into Stage 0. It is
not an HSM-backed or hardware-immutable key in the present board setup. Its
SHA-256 fingerprint is recorded in release manifests and must be compared
before provisioning. The private signing seed is offline material and is
excluded from Git.

Public-key verification and the Stage-0 boundary are `IMPLEMENTED` and `HOST
TESTED`; physical protection of Stage 0 by WRP is `DOCUMENTED ONLY` and `NOT
IMPLEMENTED`.

## Trust assumptions

The verifier, flash driver, metadata validator, linker layout, and embedded
public key are trusted software. The Cortex-M4 MPU is a fault barrier, not a
separate security world: all application code currently runs privileged and
could reconfigure it. RDP and WRP are intentionally unchanged in this work.

The confirmed metadata version is the software rollback floor. A candidate
version is not a floor while `WRITING`, `CANDIDATE_READY`, `PENDING_TRIAL`, or
`REJECTED_INVALID`; after rejection the signed manifest of the confirmed
fallback image is used. The STM32F429 has no hardware monotonic counter in this
design, so this protection is vulnerable to an attacker who can rewrite all
software metadata before RDP/WRP protection. Key rotation is deliberately not
implemented in-field; the ADR records why this is the lower-risk choice for
the fixed layout.

## Evidence map

| Claim | Evidence | Status |
|---|---|---|
| Signed image validation | `signed_image.c`, host verifier and standard vectors | `HOST TESTED` |
| Stage-0 write boundary | restricted flash instance and storage tests | `HOST TESTED` |
| MPU policy descriptors | `mpu_policy.c`, `tests/mpu_policy` | `HOST TESTED` |
| MPU enforcement on target | `mpu_null_access` fault and trial fallback on STM32F429 | `HARDWARE VALIDATED` for null-access path; other scenarios open |
| RDP2 irreversibility | device policy and manual checklist | `DOCUMENTED ONLY` |
