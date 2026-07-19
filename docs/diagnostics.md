# Diagnostics

Diagnostics expose only documented identity registers and explicitly selected
RCC, GPIOA, SCB, NVIC, SysTick, MPU, Flash, and DBGMCU registers. Reads with
potentially state-changing clock requirements are reported unavailable.
`memory regions` prints generated-layout boundaries only; it never reads
arbitrary addresses.

Runtime telemetry and memory-integrity reports are observations for laboratory
analysis. They do not make an image trusted and are not inputs to secure-boot
slot selection. Trust remains bounded by the signed-image verifier, metadata
recovery, trial-boot policy, and confirmation API.

The production-compatible layout has no spare persistent diagnostic flash
sector. Experiment telemetry is RAM-only and can be printed with
`telemetry show`. Reset causes are captured before `RCC_CSR` reset flags are
cleared; `reset decoded` names the raw flags without claiming attack causality.

For pre-hardware reference manifests, canary patterns, telemetry decoding, and
comparison workflow, see `docs/pre_hardware_validation.md`.
