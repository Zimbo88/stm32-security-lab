# Changelog

All notable project changes are documented in this file.

The format follows Keep a Changelog, and public releases use semantic
versioning.

## [1.1.0-rc1] - 2026-07-31

### Added

- Part 1 recovery, trial-boot, watchdog and signed UART recovery hardening.
- Part 2 root-of-trust, rollback, MPU and memory-boundary documentation and
  tests.
- Part 3 host-only fuzzing, property tests, coverage, sanitizer and static
  analysis profiles.
- Part 4 open-source entry documentation, CI profiles, SBOM, provenance,
  publication scanning and deterministic local release-candidate tooling.

### Changed

- Hardened reset command handling and rejected-candidate telemetry after final
  STM32F429 hardware regression.
- Removed tracked flash baselines, raw hardware logs and analyzer captures from
  the public release tree; local campaign output is ignored.
- Made hardware helper UART selection explicit instead of embedding a local
  device path.

### Security

- Documented live wrong-key, invalid-signature, rollback, trial-fallback and
  Stage-0-integrity evidence.
- Added final RDP2 No-Go and WRP-not-validated status to the public evidence.

### Known limitations

- The controlled Power-Loss campaign and final corruption rerun are incomplete.
- The fully independent UART-only reset/recovery lifecycle is limited by the
  available hardware reset wiring.
- RDP2 and WRP are disabled; no external security review or certification was
  performed.
- Published example firmware uses a clearly marked synthetic test key.

## [Unreleased]

Future changes will be recorded here after the release candidate.

## [1.0.2] - 2026-07-27

### Changed

- Clarified the difference between default Slot A/Slot B version-2 factory
  artifacts and strictly higher-version packages required for positive
  rollback-protected A/B update tests.
- Added release guidance for publishing Slot B version 3 and Slot A version 4
  update packages as hardware-validation examples without changing the package
  format or firmware trust model.

## [1.0.1] - 2026-07-27

### Added

- Hardware-validation release notes for the secure-boot and dual-slot
  secure-update chain.
- Open-source readiness documentation that separates hardware-validated,
  host-tested, experimental, and deferred production areas.
- New developer-focused README covering architecture, build, flashing, tests,
  secure boot, secure update, RSM, release process, troubleshooting, FAQ, and
  roadmap.

### Changed

- Updated release, architecture, threat-model, hardware-test, and platform
  documentation to match the current secure-update-v2 implementation.
- Clarified that authenticated UART update transport exists as a research
  implementation, while production remote-update authorization and key custody
  remain out of scope.
- Clarified the normal EXP066 LED heartbeat as an LED4-only 100 ms on /
  900 ms off pattern.
- Updated CI documentation and release guidance for the current
  Secure-Update-v2 workflow.

### Fixed

- Removed stale documentation that described the UART update path and binary
  protocol as not implemented.
- Removed stale release wording that treated the existing `v1.0.0` tag as
  unreleased.
- Corrected stale hardware-test examples that hard-coded a device path inside
  snippets despite defining `PORT`.

## [1.0.0] - 2026-07-19

### Added

- Initial public research baseline for STM32F429 secure boot.
- Stage-0 bootloader with SHA-512 payload verification and Ed25519 manifest
  authentication.
- Manifest, vector-table, address-range, and rollback-floor validation.
- Redundant metadata, initial A/B slot policy groundwork, host tooling, and
  hardware-in-the-loop framework foundation.
- BSD 3-Clause License and initial publication policy files.
