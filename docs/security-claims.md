# Security Claims And Evidence

The following table prevents host evidence from being presented as a silicon
or production guarantee.

| Claim | Implementation | Host evidence | Hardware evidence | Limitation |
|---|---|---|---|---|
| Unsigned firmware is rejected | Stage-0 Ed25519 and manifest verifier | C/Python negative tests and fuzz harnesses | Rejection behavior covered by prior hardware campaign | Depends on trusted Stage 0 and public key |
| Wrong-key packages are rejected | Public-key verification before commit | Signature/property tests | Wrong-key UART update rejected with `VERIFY` on 2026-07-31 | Depends on trusted Stage 0 and public key |
| Rollback is rejected | Confirmed-version floor in metadata policy | Policy, parser and mutation tests | Older and same-version device-key packages rejected with `ROLLBACK` on 2026-07-31 | Software-backed monotonic state |
| Stage 0 is outside updater bounds | Explicit flash-range and target-slot checks | C boundary tests and GCC analyzer | Not destructively exercised in this part | WRP is not enabled |
| Failed trial boots fall back | Persistent trial policy and confirmed slot selection | C policy model and recovery tests | IWDG candidate v49/v53 fell back; prior UART evidence records TRIAL and FALLBACK | Power-loss timing remains open |
| UART recovery requires signatures | Recovery shares package verification | Package and parser negative tests | Wrong-key UART rejection | UART electrical robustness is board-specific |
| MPU blocks tested invalid accesses | Cortex-M4 MPU policy | MPU host tests | MPU enabled on normal v52/v54; trial-fault fallback observed, retained fault context incomplete | MPU is not TrustZone |
| Interrupted updates do not replace the confirmed image | WRITING/CANDIDATE_READY commit policy and bounded trial | Update-storage and metadata tests | Prior HIL restore/corruption evidence; no physical power-loss result | Power-loss campaign is still open |
| Rejected candidate is not reported as trusted running firmware | State-gated telemetry and confirmation version reporting | `test_rejected_candidate_version_is_not_reported_as_running_image` | v53 rejected candidate reported as `firmware_version unavailable` after fallback | Application identity remains diagnostic, not attestation |
| Builds are reproducible | Prefix maps, fixed build IDs and deterministic checker | Two exported-tree comparisons pass | Not a hardware security claim | Toolchain identity remains part of provenance |
| No private key is committed | Ignore rules and publication/private-key scans | Scan passes on tracked tree and candidate | Not applicable | Human review remains required |
| Release metadata is auditable | Candidate manifest, SBOM, provenance and hashes | Local generation/verification | Not applicable | Remote CI and publication are still open |
