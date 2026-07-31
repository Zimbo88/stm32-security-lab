# Security Claims And Evidence

The following table prevents host evidence from being presented as a silicon
or production guarantee.

| Claim | Implementation | Host evidence | Hardware evidence | Limitation |
|---|---|---|---|---|
| Unsigned firmware is rejected | Stage-0 Ed25519 and manifest verifier | C/Python negative tests and fuzz harnesses | Rejection behavior covered by prior hardware campaign | Depends on trusted Stage 0 and public key |
| Wrong-key packages are rejected | Public-key verification before commit | Signature/property tests | Wrong-key UART update rejected in Part 3 | No production-key update rerun in Part 3 |
| Rollback is rejected | Confirmed-version floor in metadata policy | Policy, parser and mutation tests | Same/lower versions rejected in prior campaign | Software-backed monotonic state |
| Stage 0 is outside updater bounds | Explicit flash-range and target-slot checks | C boundary tests and GCC analyzer | Not destructively exercised in this part | WRP is not enabled |
| Failed trial boots fall back | Persistent trial policy and confirmed slot selection | C policy model and recovery tests | Prior watchdog/trial evidence | Power-loss timing remains open |
| UART recovery requires signatures | Recovery shares package verification | Package and parser negative tests | Wrong-key UART rejection | UART electrical robustness is board-specific |
| MPU blocks tested invalid accesses | Cortex-M4 MPU policy | MPU host tests | Prior MPU normal-operation evidence; full fault matrix remains limited | MPU is not TrustZone |
| Builds are reproducible | Prefix maps, fixed build IDs and deterministic checker | Two exported-tree comparisons pass | Not a hardware security claim | Toolchain identity remains part of provenance |
| No private key is committed | Ignore rules and publication/private-key scans | Scan passes on tracked tree and candidate | Not applicable | Human review remains required |
| Release metadata is auditable | Candidate manifest, SBOM, provenance and hashes | Local generation/verification | Not applicable | Remote CI and publication are still open |
