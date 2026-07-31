# Security testing hardening report — Part 3

Date: 2026-07-31
Branch: `test/security-fuzzing-and-coverage`
Scope: host fuzzing, parser robustness, properties, coverage, sanitizers,
static analysis and reproducible test profiles.

## Ausgangszustand

The repository already had 146 Python tests, eight C host-test Makefiles and
ASan/UBSan switches in selected C suites. Existing negative tests covered
many update, metadata, signature, UART and storage cases. No `fuzz/` tree,
libFuzzer entry point, Hypothesis properties, gcov collector, mutation smoke
test or security-profile Makefile existed. Coverage was not measured as a
repository artefact, and optional analyzer availability was not recorded.

Part 1 and Part 2 are present in the branch history through
`feature/root-of-trust-hardening`; no Part-1/Part-2 changes were reverted.
The target remains the STM32F429IGT6 `stm32f429_1m` layout with the existing
EXP045 Stage-0, A/B slots, signed UART update path, metadata journal, rollback
floor and MPU policy.

## Implementierte Änderungen

- Added `fuzz/harnesses/host_fuzz_driver.c`, which links production C code and
  exercises UART frames, package/header verification, redundant metadata and
  slot policy with an independent expected model.
- Added an optional LLVM libFuzzer entry point and portable deterministic
  campaign Makefile. The portable driver is sanitizer-enabled and records
  deterministic seeds.
- Added reviewed synthetic hex corpora and protocol/package/metadata
  dictionaries. Local findings, build products, coverage and Hypothesis state
  are ignored.
- Added Hypothesis properties for `stm32ctl` frame round trips, malformed
  frames, response/info/status parsing, package inspection, signed-package
  mutation rejection, fingerprints and layout vector mapping.
- Added Python and C coverage profiles. C gcov data excludes test doubles and
  Monocypher from the project aggregate while retaining per-file output.
- Added ASan/UBSan and separate UBSan smoke support, GCC `-fanalyzer`, scoped
  Ruff/mypy/Bandit checks and a bounded mutation-testing prototype.
- Added root `test-fast`, `test-security`, `fuzz`, `coverage`, `sanitize`,
  `static-analysis` and `mutation` targets. No CI workflow, release or
  public push was added.
- Added the security-test surface inventory, fuzzing procedure/results,
  coverage, sanitizer, property, profile and limitation documentation.
- Added Hypothesis to the ordinary test requirements so the property suite is
  not silently skipped in a standard test environment.
- Normalized two pre-existing Ruff import-spacing findings in the key-tool
  source/test pair; this changes no behaviour.

## Geänderte Dateien

The changes are grouped as follows:

| Group | Files | Reason |
|---|---|---|
| Test orchestration | `Makefile`, `.coveragerc`, `requirements.txt`, `requirements-security.txt` | reproducible local profiles and pinned security-test dependencies |
| C fuzzing | `fuzz/Makefile`, `fuzz/harnesses/*`, `fuzz/corpus/*`, `fuzz/dictionaries/*`, `fuzz/README.md` | production-parser host harnesses, corpora and optional libFuzzer |
| Python testing | `tests/test_security_properties.py`, `fuzz/scripts/run_campaign.py` | Hypothesis and deterministic campaign runner |
| Measurement/tools | `tools/collect_c_coverage.py`, `tools/run_static_analysis.py`, `tools/mutation_smoke.py` | gcov, analyzer and mutation evidence |
| Existing test build | `tests/update_protocol/Makefile`, `tests/update_storage/Makefile` | opt-in `COVERAGE=1` only |
| Quality hygiene | `.gitignore`, `tools/key_management.py`, `tests/test_key_management.py` | ignore local outputs and fix import-only Ruff findings |
| Documentation | `docs/security-test-*.md`, `docs/fuzzing*.md`, `docs/coverage.md`, `docs/property-testing.md`, `docs/sanitizer-testing.md`, `docs/static-analysis.md`, `docs/platform-feature-matrix.md`, `README.md` | evidence, limitations and discoverability |

## Fuzzing-Harnesses

| Harness | Engine | Corpus/dictionary | Sanitizer | Result |
|---|---|---|---|---|
| UART C parser | portable deterministic; optional libFuzzer | `corpus/uart`, `dictionaries/uart.dict` | ASan/UBSan | no finding in 1,000,000 executions |
| metadata decoder | portable deterministic; optional libFuzzer | `corpus/metadata`, `dictionaries/metadata.dict` | ASan/UBSan | no finding in 1,000,000 executions |
| package/full wrapper | portable deterministic; real C crypto wrapper | deterministic generated test-signed package | ASan/UBSan | no finding in 10,000 executions |
| boot policy/model | portable deterministic | metadata corpus | ASan/UBSan | no model mismatch in 1,000,000 executions |
| `stm32ctl` responses | Hypothesis | generated bounded bytes | Python runtime | no unexpected exception in 200 examples |

`clang` is not installed in the current environment, so
`make -C fuzz libfuzzer` was attempted and returned the documented
“libFuzzer unavailable” result. No libFuzzer execution is claimed.

## Gefundene Fehler

No reproducible memory-safety, undefined-behaviour, parser crash, timeout or
policy-model mismatch was found in the executed campaigns. The initial
mutation prototype exposed an import-path flaw in the prototype itself: both
mutants were initially reported as killed because the temporary child could
not import `release_artifacts`. The runner was corrected to provide the
repository `tools` path, and the two mutants were then killed by the intended
boundary/magic assertions. This was a `TEST-ONLY` infrastructure defect, not
a product finding, and is covered by the successful mutation command.

The first final-profile attempt also exposed an indentation defect in the new
gcov collector: its `gcov` subprocess was not assigned on the executed path.
The collector was corrected, run independently successfully, and then the
complete `test-security` profile passed. This was likewise a `TEST-ONLY`
infrastructure defect.

## Hosttests

Baseline before changes:

```text
python3 -m pytest -q -p no:cacheprovider tests                         PASS (146)
all eight existing C host suites with `make clean test`                  PASS
```

Final Python/property suite:

```text
/tmp/stm32-security-lab-security-venv/bin/python -m pytest -q \
  -p no:cacheprovider tests                                         PASS (156)
```

The security profile completed:

```text
make test-security PYTHON=/tmp/stm32-security-lab-security-venv/bin/python \
  SECURITY_FUZZ_ITERATIONS=10000                                      PASS
```

This included all 156 Python tests, C sanitizer smoke, a 10,000-execution
campaign per mode, gcov collection, GCC analyzer, scoped Ruff, mypy, Bandit,
private-key scan and `git diff --check`.

The mutation prototype completed with both mutants killed:

```text
python3 tools/mutation_smoke.py \
  --json-output fuzz/findings-local/mutation.json                       PASS
```

## Build- und Artefaktprüfung

The target builds completed with the repository's `-Werror` settings:

```text
make -C firmware/exp045_bootloader_v2 clean all report LAYOUT_PROFILE=stm32f429_1m PASS
make -C firmware/exp066_research_platform_core clean all SLOT=a LAYOUT_PROFILE=stm32f429_1m PASS
make -C firmware/exp066_research_platform_core clean all SLOT=b LAYOUT_PROFILE=stm32f429_1m PASS
```

The reproducibility check also passed:

```text
/tmp/stm32-security-lab-security-venv/bin/python tools/check_deterministic_build.py PASS
```

The synthetic marker verifier passed with `all_verified: true` for the
bootloader, both slot images, SRAM symbols and the configuration reference:

```text
/tmp/stm32-security-lab-security-venv/bin/python tools/rdp2_marker.py inspect \
  --output /tmp/stm32-security-part3-marker-report.json                  PASS
```

The private-key scan passed. `git diff --check` passed, and the final branch
status was clean. `make -C fuzz libfuzzer` was attempted separately but is
documented as unavailable because `clang` is not installed.

## Fuzzingkampagnen

The extended bounded campaign was executed with `make fuzz`. It used a fixed
portable LCG, real production C functions and ASan/UBSan:

| Mode | Executions | Duration | Corpus | Findings |
|---|---:|---:|---:|---|
| UART | 1,000,000 | 0.335 s | 29 bytes | none |
| metadata | 1,000,000 | 0.128 s | 83 bytes | none |
| policy model | 1,000,000 | 0.949 s | 83 bytes | none |
| package/full wrapper | 10,000 | 0.016 s | 640 bytes | none |

The lower full-crypto count is intentional and documented. The campaign is
limited evidence, not a claim of exhaustive coverage-guided fuzzing.

## Coverage

Python branch coverage was measured with 2,840 own-tool statements and 902
branches: 33% aggregate line coverage. `tools/stm32ctl/protocol.py` reached
84% line coverage. The low aggregate reflects command-line and hardware helper
modules not invoked by the unit suite.

C gcov measured project-source aggregates, excluding test drivers, simulated
flash, stubs and Monocypher:

| Suite | Project line | Project branch | Critical parser/policy line | Critical parser/policy branch |
|---|---:|---:|---:|---:|
| `tests/update_protocol` | 65.03% | 75.40% | 67.59% | 71.32% |
| `tests/update_storage` | 77.27% | 89.26% | 76.68% | 88.12% |

The protocol-suite target is below the documented 90%/85% engineering goal.
The gap remains visible and is not hidden with exclusions.

## Sanitizer

Executed commands:

```text
make -C fuzz sanitize                                           PASS
make -C fuzz clean all SANITIZERS=undefined                     PASS
make -C tests/host_verifier clean test SANITIZE=1                PASS
make -C tests/update_protocol clean test SANITIZE=1              PASS
make -C tests/update_storage clean test SANITIZE=1               PASS
```

No ASan/UBSan finding occurred. The target-only MMIO and physical flash paths
are not sanitizer-executable.

## Statische Analyse

```text
python3 tools/run_static_analysis.py                              PASS
```

GCC 13 `-fanalyzer` returned zero findings for the selected own C modules.
Ruff, mypy and Bandit passed for the new testing tools and property suite;
full-repository Ruff also passes after the import-only hygiene change.
`cppcheck`, `clang-tidy`, `scan-build` and Valgrind are not installed and were
not represented as successful runs.

## Property- und Model-Tests

Hypothesis generated bounded round trips, malformed-frame cases, strict
response lengths, arbitrary package inspection and signed-package mutations.
The final property file contains 10 tests and passed. The C policy harness
compared decoded metadata decisions against production policy for one million
mutated cases without a mismatch. Mutation smoke killed both selected mutants.

## Hardware-Regression

Hardware was present and identified read-only:

```text
ST-Link serial: <redacted>
MCU: STM32F42x/F43x, chip ID 0x419
Flash: 1048576 bytes
SRAM: 262144 bytes
UART: /dev/ttyUSB0, 115200 baud
```

Executed safe checks:

| Test | Procedure | Result | Evidence |
|---|---|---|---|
| `stm32ctl info` | reversible ST-Link reset, then UART binary handshake | PASS | `/tmp/stm32_security_part3_reset.log` and command output |
| `stm32ctl status` | reversible ST-Link reset, then UART status | PASS; idle session, status OK | `/tmp/stm32_security_part3_status.json` |
| wrong-key update | temporary synthetic test seed, Slot-A version 100, UART update | target rejected `BEGIN_UPDATE` with `VERIFY` | `/tmp/stm32_security_part3_wrong_key.json` |
| post-rejection state | reversible reset, UART status | PASS; idle/status OK | `/tmp/stm32_security_part3_post_reject_status.json` |

The wrong-key package was never accepted by the target and no valid production
key was used. The normal firmware, valid update, rollback, watchdog, MPU,
health-gate and confirmation hardware evidence remains the Part-1/Part-2
record; this Part-3 host-only change did not alter target firmware. A fresh
valid production-key update was not rerun because the corresponding private
key is intentionally absent from the workspace. No hardware flashing,
metadata writes, option-byte access or destructive test was performed in this
part.

## Nicht getestete Punkte

- coverage-guided libFuzzer/AFL++ campaigns because `clang`/AFL++ are absent;
- full Python tool coverage targets and the remaining C protocol branches;
- Valgrind, cppcheck, clang-tidy and scan-build;
- formal verification, model checking beyond the bounded policy model and
  external cryptographic review;
- target firmware rebuild/install followed by a fresh valid signed update;
- physical power-loss, brownout, fault injection, glitching or side-channel
  testing;
- RDP2 and Write Protection behavior.

## Verbleibende Grenzen

The test engine is deterministic but not coverage-guided. Coverage is not a
security proof. The C policy model could contain a specification error and is
therefore kept small and paired with production transition tests. Third-party
Monocypher is not independently audited here. Target-only timing, MMIO,
interrupt and flash effects remain hardware concerns. RDP2 and WRP remain
disabled.

## Bewertung von 0 bis 10

| Area | Rating | Evidence basis |
|---|---:|---|
| Parserrobustheit | 8 | production C parsers, negative tests, sanitizer fuzz |
| Fuzzing | 7 | one-million deterministic campaigns; no libFuzzer |
| Property Testing | 8 | Hypothesis plus C policy model |
| C Coverage | 7 | gcov branch reports, below protocol goal |
| Python Coverage | 6 | measured but 33% aggregate |
| Sanitizer | 8 | ASan/UBSan suites and fuzz driver |
| statische Analyse | 7 | GCC analyzer/Ruff/mypy/Bandit; optional tools absent |
| Compilerhärtung | 7 | `-Werror` plus warning review; target-wide conversion flags remain open |
| Regressionstests | 8 | 156 Python tests, existing C suites, mutation smoke |
| Testreproduzierbarkeit | 9 | pinned dependencies, fixed seeds and local profiles |
| Hardware-Regression | 6 | safe UART info/status/wrong-key evidence only in this part |
| Dokumentation | 8 | surface, commands, evidence and limitations recorded |
| unabhängige Nachvollziehbarkeit | 8 | exact commands, statuses, seeds and limitations |

No rating above 9 is warranted: extended coverage-guided fuzzing, broader
coverage and fresh valid-key hardware regression remain open.

## Sicherheitsgrenze

RDP2 wurde nicht aktiviert.

Write Protection wurde nicht aktiviert.

Option Bytes wurden nicht verändert.

Es wurde keine physische Power-Loss-Kampagne durchgeführt.
Es wurde kein öffentlicher Push und kein Release durchgeführt.

SECURITY TEST INFRASTRUCTURE COMPLETE – EXTENDED CAMPAIGNS REMAIN
