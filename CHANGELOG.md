# Changelog

All notable project changes are documented in this file.

The format follows Keep a Changelog, and public releases use semantic
versioning.

## [Unreleased]

### Added

- Automated hardware-in-the-loop validation for the secure-boot chain.
- Positive and negative manifest, hash, signature, and authentication tests.
- Flash backup and post-test restoration verification.
- Publication-readiness documentation and repository policy files.
- BSD 3-Clause License.

### Changed

- Payload mutation tests preserve the application vector-table prefix.
- ST-Link failures include command output, return codes, and retry diagnostics.
- Public documentation is being consolidated into consistent technical English.

### Fixed

- Modified-payload HIL images no longer corrupt the initial stack pointer.
- Hardware restoration failures expose the underlying `st-flash` diagnostics.

## [1.0.0] - Unreleased

Initial public research release.
