# Open-Source Release Readiness Report

Status of this report: local Part-4 work on
`chore/open-source-release-readiness`. It is an engineering record, not a
public release note. The final command results below are updated after the
clean-checkout run.

## Ausgangszustand

The repository already contained the Part-1 recovery/watchdog work, the Part-2
secure-boot/key/MPU work and the Part-3 host security-test infrastructure. It
also contained a single broad GitHub Actions workflow, pinned root Python
requirements, the legacy `release_artifacts.py` verifier, the EXP066 package
tooling, a deterministic-build checker and partial release documentation.

The missing release-readiness pieces were a coherent fresh-clone entry point,
explicit community policies and templates, a documentation index, a hardware
compatibility matrix, a deterministic SPDX inventory, provenance and candidate
verification, publication scanning, and separated CI workflow definitions.
The existing Monocypher 4.0.3 archive and its checksum were retained; its
dual CC0-1.0/2-clause-BSD licensing is recorded in the notices.

## Implementierte Änderungen

- Added a project-scope and evidence vocabulary for embedded developers,
  reviewers, students, maintainers and STM32F429 researchers.
- Added fresh-clone quickstart, toolchain, hardware-compatibility,
  versioning, development-workflow and maintainer documentation.
- Added `CODE_OF_CONDUCT.md`, expanded `CONTRIBUTING.md` and `SECURITY.md`,
  four issue templates, a security-report template and a pull-request
  checklist. No fictitious security email or CODEOWNER was added.
- Added documentation and ADR indexes and an auditable security-claims table.
- Added `tools/generate_sbom.py`, producing deterministic SPDX-2.3 JSON from
  the pinned requirement files and the vendored Monocypher archive.
- Added `tools/release_candidate.py` and
  `tools/verify_release_candidate.py`. They build and verify a local
  `v1.1.0-rc1-local` directory and deterministic archive. They never flash,
  write Option Bytes, set RDP/WRP, tag, push or create a GitHub release.
- Added `tools/check_documentation.py` and `tools/check_workflows.py` for
  offline link and workflow-structure checks.
- Added publication scanning with explicit handling for tracked historical
  warnings and candidate errors; documented hardware serials were redacted.
- Added root Make targets `release-candidate`, `verify-release` and
  `publication-scan`; added pinned PyYAML to the security test profile.
- Added separate local-validation workflows for firmware, security tests and
  documentation, while retaining the existing host/firmware workflow.
- Added a release-artifact definition, third-party notices and release
  checklist extensions for SBOM, provenance, candidate archives and remote-CI
  status.

## Geänderte Dateien

The main groups are:

- `README.md`, `CONTRIBUTING.md`, `SECURITY.md`, `CODE_OF_CONDUCT.md`,
  `CHANGELOG.md`, `.tool-versions`, `THIRD_PARTY_NOTICES.md`;
- `.github/ISSUE_TEMPLATE/*`, `.github/PULL_REQUEST_TEMPLATE.md`,
  `.github/dependabot.yml` and `.github/workflows/*`;
- `docs/README.md`, `docs/project-scope.md`, `docs/quickstart.md`,
  `docs/toolchain-support.md`, `docs/hardware-compatibility.md`,
  `docs/versioning.md`, `docs/security-claims.md`,
  `docs/release-artifacts.md`, `docs/licenses.md`,
  `docs/development-workflow.md`, `docs/maintainer-guide.md`,
  `docs/adr/README.md`, `docs/RELEASE_CHECKLIST.md` and this report;
- `tools/generate_sbom.py`, `tools/release_candidate.py`,
  `tools/verify_release_candidate.py`, `tools/publication_scan.py`,
  `tools/check_documentation.py`, `tools/check_workflows.py`;
- `Makefile`, `requirements-security.txt`,
  `tools/secure_boot_hil/pyproject.toml`, `.gitignore` and
  `tests/test_release_readiness.py`;
- historical hardware evidence files where the real ST-Link serial was
  replaced with `<redacted>`.

No firmware security policy or flash-writing path was changed by Part 4.

## Quickstart

The documented path is:

```bash
python3 -m venv .venv
. .venv/bin/activate
python -m pip install -r requirements-security.txt
make test-fast PYTHON=python
make test-security PYTHON=python
make release-candidate RELEASE_TEST_KEY=1
make verify-release RELEASE_DIR=dist/v1.1.0-rc1-local
```

The initial dependency install needs a package source; the normal tests and
candidate verification are offline after installation. A fresh-checkout
rehearsal is required before calling the result reproducible.

## CI

| Workflow | Trigger | Local status | Remote status |
|---|---|---|---|
| `ci.yml` | push, pull request | YAML and commands inspected | not executed on GitHub in this task |
| `firmware-build.yml` | push, pull request | YAML and build commands inspected | not executed on GitHub in this task |
| `security-tests.yml` | push, pull request | YAML and local profile command inspected | not executed on GitHub in this task |
| `docs.yml` | push, pull request | YAML, links, publication scan inspected | not executed on GitHub in this task |

All workflows request `contents: read`, define timeouts and use the existing
SHA-pinned checkout/setup actions. No workflow has secrets, hardware access,
Option-Byte commands or release write permissions. Dependabot is monthly for
the root and HIL Python inputs and GitHub Actions.

## SBOM

The generated artifact is deterministic SPDX 2.3 JSON. It records the project,
Monocypher 4.0.3 archive and pinned Python dependencies from
`requirements.txt` and `requirements-security.txt`, including package license
identifiers, download locations and the vendored archive SHA-256. The local
test hash was:

```text
6e0d491466a2536728f7490c79348accbcc0f7b6a6a9b1738cba5b990812d845
```

This is an application/dependency inventory, not a complete operating-system
or compiler SBOM.

## Release Candidate

The local name is `v1.1.0-rc1-local`; it is not a Git tag, public version or
GitHub release. The candidate contains bootloader ELF/BIN/HEX, legacy EXP065
artifacts, Slot-A/B ELF/BIN/HEX and signed update packages, marker evidence,
memory metadata, `SHA256SUMS`, SBOM, provenance, notices and changelog.

The rehearsal used the deterministic CI test seed `bytes(range(32))`. Its
signing public-key fingerprint is:

```text
56475aa75463474c0285df5dbf2bcab73da651358839e9b77481b2eab107708c
```

The Stage-0 embedded public-key fingerprint is:

```text
482dd9daac3d406f779995a50a00eb2ac9948eb412cba4403e2f81091780499a
```

Because these differ, the test-key candidate is intentionally marked
`hardware_installable: false`. No private key is copied to the candidate.
An operator-supplied seed is accepted only by explicit path and is never
printed or archived.

The candidate verification checks exact artifact hashes, SBOM and provenance
hashes, public-key length/fingerprint, marker completion, legacy signature,
both slot package signatures, SHA256SUMS, archive hash/file set and the
publication scan. Hardware is not touched.

## Reproduzierbarkeit

Two fresh checkouts of the same committed branch are required to run the
host profile, firmware builds, candidate generation and candidate verification.
The archive hash is compared after each build. At commit
`d2cd76418f0abf1ddada3525349d03ac439d481a`, two fresh checkouts produced the
following results:

```text
checkout A: 158 passed, 1 skipped; candidate build and verification: PASS
checkout B: 158 passed, 1 skipped; candidate build and verification: PASS
archive A: 41bad388f663abcd847fe68a3a73fb9d3da842b5de5b450fd559b0319e2b545a
archive B: 41bad388f663abcd847fe68a3a73fb9d3da842b5de5b450fd559b0319e2b545a
```

The first in-tree rehearsal was run with `--allow-dirty` while the Part-4
files were being developed. A subsequent clean comparison initially exposed
volatile verification timestamps in copied package reports; the release
candidate tool now normalizes only those diagnostic timestamps while leaving
the underlying verification and signatures unchanged. The clean comparison
above was then repeated successfully.

## Hardwareevidenz

Hardware was available and was used only for reversible diagnostics and
rejected protocol checks. The board was identified read-only with:

```text
st-info --probe
MCU family: STM32F42x/F43x
chip ID: 0x419
flash: 1048576 bytes
SRAM: 262144 bytes
```

The physical UART was the documented USART1 connection; the local device name
is intentionally omitted from this public report. The safe reset command was
`st-flash reset`. The tool reported that NRST is not connected and used the
software AIRCR reset. No flash read/write command and no Option-Byte command
was executed.

Actual Part-4 checks and local log locations:

| Check | Result | Evidence |
|---|---|---|
| `stm32ctl info` before reset | timeout | `hil-results/secure-update-20260731T143300Z/` |
| software reset, then `stm32ctl info --json` | PASS; protocol 1, 1024-byte maximum payload, 10-byte header, 4-byte CRC | `hil-results/part4-safe-reset-20260731T143353Z/` |
| `stm32ctl status` immediately after a reset | PASS; session 0, status OK, no accepted bytes/programmed blocks | `hil-results/part4-status-after-reset-20260731T143423Z/` |
| status without a fresh reset | timeout; reset is required for this observed session state | `hil-results/part4-status-20260731T143400Z/` |
| synthetic wrong-key Slot-B package | PASS rejection; target returned `BEGIN_UPDATE rejected by target: VERIFY` | `hil-results/part4-wrong-key-20260731T143448Z/` |
| previously valid-looking Slot-B v3 package | rejected by target with `VERIFY`; no bytes accepted | `hil-results/part4-valid-a-to-b-20260731T143523Z/` |
| previously valid-looking Slot-A v4 package | rejected by target with `ROLLBACK`; no bytes accepted | `hil-results/part4-valid-b-to-a-20260731T143538Z/` |

The older Part-1/Part-3 hardware records contain the positive A-to-B and
B-to-A update runs and are linked from the recovery and security-test reports.
They are historical evidence, not a new Part-4 execution. This Part-4 run did
not claim a new valid update because the available package versions did not
match the target's current rollback state. The post-rejection status remained
at zero accepted payload bytes and zero programmed blocks.

## Publication Scan

The scan distinguishes errors from review warnings. It checks private-key
blocks, seed/key filenames, tokens, home paths, hardware serials, device
paths, dump extensions, local logs and option-byte write command patterns.

Existing historical tracked material produced warnings for legacy raw logs,
logic captures, `/dev/ttyUSBx` examples and documented planning commands. These
are not treated as clean release artifacts and remain a review item. Real
ST-Link serial numbers in the documented hardware evidence were redacted. The
candidate scope is required to have no error findings; a generic `/dev/ttyUSBx`
example may remain a warning because the quickstart deliberately uses an
abstract device name.

## Community- und Maintainer-Reife

The repository now has contributor guidance, private vulnerability-reporting
instructions using GitHub's advisory channel, a Contributor Covenant 2.1
reference, issue/PR templates, a release checklist, a maintainer guide and
explicit evidence labels. No public issue is recommended for confidential
security details. No external contributor or independent security review was
performed in this task.

## Nicht getestete Punkte

- GitHub Actions were not executed remotely.
- No public release, tag, push or external contributor test was performed.
- No external security review or complete long-term fuzzing campaign was
  performed.
- No final hardware campaign, power-loss campaign, WRP activation or RDP2
  provisioning was performed.
- The local candidate uses a test key and is not a production-like hardware
  release.
- Container and hardware device passthrough were evaluated but not introduced;
  the native setup is smaller and hardware access remains board-specific.

## Bewertung (0–10)

| Bereich | Bewertung | Begründung |
|---|---:|---|
| README | 8 | Clear entry point and evidence links; independent review remains. |
| Quickstart | 8 | Fresh-clone commands are documented and locally exercised. |
| Buildreproduzierbarkeit | 8 | Two fresh checkouts produced identical verified candidate archives; the workflow remains local rather than remote. |
| CI-Design | 8 | Separate read-only jobs, timeouts and pinned actions. |
| CI-Evidenz | 5 | Workflows are local-validated, not remotely executed here. |
| Releasewerkzeuge | 8 | Candidate build and offline verification cover the intended artifact path. |
| Releaseartefakte | 8 | Firmware, packages, manifests, hashes, SBOM and provenance are defined. |
| SBOM | 7 | Deterministic SPDX inventory exists; OS/compiler transitive inventory is out of scope. |
| Lizenztransparenz | 8 | Monocypher and pinned Python inputs are documented with notices. |
| Supply-Chain-Nachvollziehbarkeit | 7 | Provenance is explicit and SLSA-inspired, not an attestation. |
| Security Policy | 8 | Private reporting and key/dump restrictions are clear. |
| Contribution Workflow | 8 | Templates, evidence vocabulary and review guidance exist. |
| Hardwaredokumentation | 8 | Reference MCU/layout/pins and evidence limits are documented. |
| Maintainer-Reife | 8 | Release, backup, key and handoff procedures are documented. |
| Publication Safety | 7 | Errors are caught and identifiers were redacted; legacy warning review remains. |
| Open-Source-Gesamtreife | 8 | Strong local preparation; remote CI and final hardware evidence remain. |

No score above 9 is assigned because remote CI, external review and final
hardware evidence are absent.

## Sicherheitsgrenze

RDP2 was not enabled. Write Protection was not enabled. Option Bytes were not
changed. No physical power-loss campaign was performed. No public push, tag or
GitHub release was performed.

OPEN-SOURCE RELEASE READINESS INCOMPLETE
