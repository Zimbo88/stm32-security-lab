# Architecture Decision Records

ADRs record decisions, not guarantees. Each entry has a status and links to
the implementation and evidence available in the repository.

| ADR | Decision | Status |
|---|---|---|
| [ADR-001](ADR-001-key-rotation-strategy.md) | Keep the root public key fixed; do not add untested key rotation | Accepted |
| Stage-0 boundary | Normal and recovery update paths cannot write Stage 0 | Accepted in implementation/docs |
| Software rollback floor | Use redundant metadata and a software-backed confirmed-version floor | Accepted in implementation/docs |
| MPU policy | Use Cortex-M4 MPU as error/limited exploit containment, not TrustZone | Accepted in implementation/docs |
| Recovery policy | Prefer confirmed fallback and signed UART recovery | Accepted in implementation/docs |
| RDP2 strategy | Keep RDP2 manual and disabled until final hardware evidence | Accepted in checklist/docs |
| Write Protection | Evaluate only; do not enable automatically | Accepted in evaluation |
| Fuzzing strategy | Host-only production-parser harnesses with deterministic smoke/long profiles | Accepted in Part 3 |
| Release artifacts | Local candidate manifest, SPDX SBOM, provenance and deterministic archive | Proposed in Part 4 |

No CODEOWNERS file is added: only one maintainer is currently identified and
inventing additional GitHub owners would be misleading.
