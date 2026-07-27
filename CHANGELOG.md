# Changelog

All notable project changes are documented in this file.

The format follows Keep a Changelog, and public releases use semantic
versioning.

## [Unreleased]

- No unreleased changes are documented yet.

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
- Corrected stale hardware-test examples that hard-coded `/dev/ttyUSB0` inside
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
