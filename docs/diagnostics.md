# Diagnostics

Diagnostics are separated into public, restricted, and secret information
classes. Public output is intended for reproducible host and HIL parsing.
Restricted output contains raw UID, exact option bytes, exact memory-region
addresses, retained fault PC/LR details, raw register snapshots, exact
vector-table failure addresses, and detailed runtime telemetry. It is disabled
by default and is only emitted when EXP066 is built with
`RSM_RESTRICTED_DIAGNOSTICS=1`. Secret output is never available.

EXP066 still never accepts arbitrary addresses. Reads with potentially
state-changing clock requirements are reported unavailable. `memory regions`
prints generated-layout boundaries only in restricted diagnostic builds and
never reads arbitrary addresses.

Runtime telemetry and memory-integrity reports are observations for laboratory
analysis. They do not make an image trusted and are not inputs to secure-boot
slot selection. Trust remains bounded by the signed-image verifier, metadata
recovery, trial-boot policy, and confirmation API.

The production-compatible layout has no spare persistent diagnostic flash
sector. Experiment telemetry is RAM-only and can be printed with
`telemetry show` only in restricted diagnostic builds. Reset causes are
captured before `RCC_CSR` reset flags are cleared; `reset decoded` names the
raw flags without claiming attack causality.

`rsm status` emits the Runtime Security Monitor key-value schema. It
does not claim external attestation. It connects Stage-0-authenticated launch
assumptions, reset cause, health state, event counters, UID fingerprint, and
basic runtime checks into a public evidence summary. Phase 2A adds a
vector-table monitor status, failure class, check counter, failure counter, and
latched-failure flag. Public output never prints exact vector entries or
concrete handler addresses.

The UID fingerprint is CRC32 over the three 32-bit UID words serialized in
little-endian order. It is a stable diagnostic identifier, not a cryptographic
hash, not authentication material, and collisions are possible.

For pre-hardware reference manifests, canary patterns, telemetry decoding, and
comparison workflow, see `docs/pre_hardware_validation.md`.
