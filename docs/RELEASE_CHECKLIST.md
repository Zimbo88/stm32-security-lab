# Release Checklist

Use this checklist for public releases. Do not overwrite existing tags; if a
tag already exists, use the next patch release tag.

For the Part-4 local candidate, stop before the Git and Release section. No tag,
push, GitHub release, Option-Byte write, RDP2 activation or WRP activation is
performed by the repository tooling.

## Repository State

- [ ] Publication branch is clean before tagging.
- [ ] All intended source, test, CI, and documentation files are tracked.
- [ ] Generated files and local results are ignored.
- [ ] No build artifacts, HIL raw logs, flash dumps, caches, or temporary files
      are staged.
- [ ] No private hostnames, home paths, emails, serial numbers, or device
      identifiers remain in committed release notes.
- [ ] No private signing keys or signing seeds are tracked.
- [ ] Third-party license notices are preserved.

## Documentation

- [ ] README explains goal, architecture, build, flash, tests, secure boot,
      secure update, RSM, release process, troubleshooting, FAQ, limitations,
      and roadmap.
- [ ] README links to all authoritative detailed documents.
- [ ] Hardware status clearly separates real hardware evidence from host-only
      tests and experimental components.
- [ ] `CHANGELOG.md` has a finalized release section.
- [ ] `CITATION.cff` version and release date match the intended tag.
- [ ] Release notes exist under `docs/releases/`.
- [ ] RDP2 and Option-Byte limits are stated clearly.
- [ ] Known limitations and non-production boundaries are current.
- [ ] Markdown links have been checked.
- [ ] `docs/security-claims.md` maps each claim to host and hardware evidence.
- [ ] `docs/open-source-release-readiness-report.md` is reviewed.
- [ ] `THIRD_PARTY_NOTICES.md` and `docs/licenses.md` match the vendored inputs.

## Verification

- [ ] Ruff passes.
- [ ] Mypy passes for typed Python tooling.
- [ ] Complete Python test suite passes.
- [ ] C host tests pass.
- [ ] Supported ASan/UBSan host tests pass.
- [ ] Bootloader clean build and size report pass.
- [ ] EXP066 Slot A and Slot B release builds pass.
- [ ] Slot A and Slot B update packages verify with `PUBLIC_KEY_HEADER`.
- [ ] Slot A and Slot B update packages verify with `PUBLIC_KEY_HEX`.
- [ ] Wrong public key is rejected.
- [ ] Deterministic-build check passes.
- [ ] Repository audit passes.
- [ ] Private-key scan passes.
- [ ] `git diff --check` passes.
- [ ] GitHub Actions for the pushed commit are green.
- [ ] SBOM and release provenance are generated and verified.
- [ ] Publication scan is clean or every finding has a written disposition.
- [ ] Deterministic candidate archives from two clean checkouts match.

## Hardware Evidence

- [ ] RDP Level and Option Bytes were read only before and after hardware
      validation.
- [ ] Secure boot selected a concrete slot and executed SHA-512 and Ed25519.
- [ ] Slot A baseline boot was observed.
- [ ] Positive A-to-B update was observed.
- [ ] Positive B-to-A update was observed.
- [ ] Trial boot and application confirmation were observed.
- [ ] Rollback rejection was observed.
- [ ] Corrupted manifest, signature, target, and payload cases failed closed.
- [ ] UART CRC, sequence, timeout, and random-input cases failed closed.
- [ ] Flash readback matched expected artifacts for positive updates.
- [ ] Final board state was restored or intentionally documented.
- [ ] Physical power-removal and visual LED evidence are either present or
      explicitly listed as remaining manual evidence.

## Git And Release

- [ ] Commit history has been reviewed for secrets and private data.
- [ ] `git status --short` is clean.
- [ ] Existing tags and releases have been checked.
- [ ] New annotated tag uses the intended version.
- [ ] Tag points to the tested commit.
- [ ] Release artifacts are built from exactly the tagged commit.
- [ ] SHA-256 checksum file is generated.
- [ ] Release notes are published.
- [ ] No private seeds, private keys, flash dumps, or HIL raw logs are attached.

## Part 5 evidence still open

- [ ] Physical power-loss campaign.
- [ ] UART-only end-to-end campaign after all final software changes.
- [ ] Complete metadata-corruption matrix on hardware.
- [ ] Final WRP decision and manual option-byte review.
- [ ] Manual RDP2 provisioning on an expendable device, if separately approved.
