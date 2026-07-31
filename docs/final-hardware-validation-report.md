# Final hardware validation report

Date: 2026-07-31  
Branch: `test/final-hardware-validation`  
Baseline commit: `c9de8a5bb2ec16db84d9e2f517d5aff5db840956`

## Ausgangszustand

The attached target is an STM32F429IGT6-class STM32F42x/F43x device, ID
`0x419`, with 1 MiB flash and 256 KiB SRAM. USART1 is connected through a
separate FTDI USB-UART adapter at 115200 8N1, PA9 TX, PA10 RX and common GND.
NRST is not wired; the available probe reports that reset uses AIRCR software
reset. The attached ST-LINK/V2 was used for read-only identification, safe
reset, baseline readback and recovery after experiments.

Read-only Option Byte state was `OPTCR=0x0fffaaed`: RDP Level 0 and all WRP
sectors unprotected. The embedded public-key fingerprint is
`482dd9daac3d406f779995a50a00eb2ac9948eb412cba4403e2f81091780499a`.

The final restored device state is confirmed Slot A, firmware version 54. The
final metadata records were valid and showed Slot A confirmed after the
trial; the final region hashes were:

| Region | SHA-256 |
| --- | --- |
| Stage 0 | `679e88fbdc215e42c0c9637be5730b0cd04d744d2ce3ecc5cf1b5cab2fa1ba9a` |
| metadata A+B | `09531f5e8d7d1bfb9716ec4894f7fd2a17846a742d55d7af7d9e87757d5ccb4c` |
| Slot A | `554b253af9e5e41424aa6668edb9d8202300914c2666b15ef87335a60e9ab254` |
| Slot B | `df3b452fb9cab86ea1aa0d96384f58e49c4c63b9e23282d58130ad506e1a508a` |

Raw captures, flash images, HIL backups and UART logs remain under the ignored
local directory `baseline/final-hardware-campaign/`.

## Implemented changes

- `stm32ctl reset` now sends RESET without waiting for an ACK. The target
  resets immediately after accepting the command, so an ACK is not a reliable
  postcondition.
- Public application telemetry and confirmation snapshots no longer report a
  `WRITING`, `CANDIDATE_READY` or `REJECTED_INVALID` candidate version as the
  running firmware version.
- Host regression tests cover the fire-and-forget reset semantics and the
  rejected-candidate telemetry contract.
- The final baseline, WRP decision, RDP2 go/no-go record and this report were
  added. Local campaign output is excluded by `.gitignore`.

## Debuggerfreie UART-Tests

### Normal boot and B-side update

Firmware v52 was built with the existing device signing key and verified
against the Stage-0 public key. After a safe reset, the exact update command
was:

```sh
PYTHONPATH=tools python3 -m stm32ctl --port /dev/ttyUSBx --baud 115200 \
  --timeout 15 --retries 1 --json update \
  --package firmware/exp066_research_platform_core/build/part5_slot_b_v52/part5_slot_b_v52_slot_b_update_v2.bin \
  --public-key-header firmware/exp045_bootloader_v2/src/firmware_public_key.h \
  --quiet
```

Observed result: `result=ok`, image version 52, 43 programmed blocks. The
application console then reported:

```text
confirmation OK running B firmware_version 52
rsm.boot.slot=B
rsm.boot.firmware_version=52
rsm.security.vectors=pass
rsm.cpu.mpu=enabled
rsm.health.state=healthy
```

### Rejected trial and fallback

The signed test image `mpu_write_bootloader`, Slot A v53, was accepted by the
update transport and then failed during trial execution. After bounded IWDG
attempts, metadata showed the candidate as rejected and confirmed Slot B as
fallback. The fixed application output reported:

```text
confirmation NOT PENDING running B firmware_version unavailable
rsm.boot.slot=B
rsm.boot.firmware_version=unavailable
rsm.boot.last_reset=software
rsm.security.vectors=pass
rsm.evidence.last_fault=none
```

The `software` reset in this final console query is from the later deliberate
probe reset; prior IWDG evidence is retained in the watchdog campaign logs and
the Part 1 report. The important regression is that rejected v53 was not
reported as the trusted running image.

### Rollback and authorization negatives

- Older device-key package: `BEGIN_UPDATE rejected by target: ROLLBACK`.
- Same-version device-key package: `BEGIN_UPDATE rejected by target: ROLLBACK`.
- Wrong-key package: `BEGIN_UPDATE rejected by target: VERIFY`.
- Flash hash and metadata remained unchanged after the negative tests.

The public-key fingerprint used for positive packages was the embedded device
fingerprint above. No private key was copied into the repository or emitted in
logs.

### Reset command regression

The new host behavior was tested against the target:

```sh
PYTHONPATH=tools python3 -m stm32ctl --port /dev/ttyUSBx --baud 115200 \
  --timeout 2 --retries 0 --json reset
```

Observed result:

```json
{"reset": "requested", "result": "ok"}
```

This removes the previous false timeout caused by the target resetting before
the response could be read. It does not create a physical reset path; NRST is
still not connected.

## Metadatenkorruption

Earlier successful HIL runs on the same board (`hil-results/run-20260719T174955Z`
and `hil-results/run-20260719T182246Z`) covered:

- metadata A erased or zeroed;
- metadata B erased or zeroed;
- both metadata copies erased or zeroed;
- invalid metadata combined with invalid slots;
- valid/invalid slot combinations;
- erased and zeroed signed-image headers.

Those runs restored bootloader, both metadata copies and both slots with
readback verification. Their reports classify the policy observations as
`OBSERVE`, not as an unconditional proof of every fail-closed claim.

The new Part 5 attempt selected the metadata and slot cases but stopped at the
first test-image flash because the ST-Link flash loader failed while the
IWDG-enabled target was running. The backup and restore verification reported
all five declared regions matched, and a fresh Stage-0 read still matched the
baseline hash. The new run is therefore `INCONCLUSIVE`, not a passing final
matrix.

## Slot corruption

The successful prior HIL runs passed the invalid-signature, erased-header,
zero-header and both-invalid-image cases, while slot-selection cases were
recorded as observations. The current campaign did not repeat the destructive
cases after the restore failure. Stage 0 remained unchanged throughout the
new update and trial tests.

## Watchdog and MPU

The IWDG trial firmware was installed using the existing signed update path.
Prior detailed UART evidence includes:

```text
BOOT_RESET cause=IWDG raw=0x34000000
Slot decision = TRIAL
HEALTH_GATE result=NOT HEALTHY
Slot decision = FALLBACK
```

The current v53 MPU-write test also resulted in bounded trial failure and
fallback. The normal v52/v54 builds reported `mpu=enabled`,
`stack=mpu_guard_enabled`, healthy status, vector checks passing and no public
fault record. A retained, independently readable MPU fault context was not
obtained in the current v53 run. MPU hardware evidence is therefore limited
to policy activation, normal operation and indirect fault/fallback behavior;
it is not claimed as a complete fault-context proof.

## Power-Loss campaign

No physical power-loss test was executed. The host exposes no verified relay,
programmable supply or safe power switch, and NRST is not connected. No
short-circuit, undervoltage or improvised cable removal was performed.

The existing procedure in `docs/rdp2-power-loss-campaign.md` is prepared, but
the following phases remain untested on real hardware: packet reception,
`WRITING`, erase, early/middle/last program, readback, metadata copy/commit,
trial boot, health gate and confirmation. This is a mandatory RDP2 blocker.

## Recovery and final state

After the trial-fault test, a valid higher-version Slot-A package v54 was
transferred over USART1 and confirmed. The final UART evidence was:

```text
confirmation OK running A firmware_version 54
rsm.boot.slot=A
rsm.boot.firmware_version=54
rsm.security.vectors=pass
rsm.cpu.mpu=enabled
rsm.health.state=healthy
```

The Stage-0 hash before and after the campaign was identical. RDP and WRP
remained unchanged.

## WRP

Decision: **WRP RECOMMENDED BUT NOT YET HARDWARE VALIDATED**. No expendable
device was explicitly designated, so no WRP write was attempted. See
`docs/final-wrp-decision.md`.

## RDP2

The final matrix in `docs/rdp2-final-go-no-go.md` is a no-go. The blockers are
the missing controlled power-loss campaign, incomplete final corruption rerun,
lack of an independent physical reset/power path for a complete debugger-free
lifecycle, and absence of separate owner acceptance for irreversible RDP2.

## Quality checks

Executed during this campaign:

| Command/profile | Result |
| --- | --- |
| `python3 -m pytest -q tests/test_stm32ctl.py` | PASS, 11 tests |
| `make ... SLOT=b IMAGE_VERSION=52 ... all update-package inspect-update-package verify-update-package` | PASS |
| `make ... SLOT=a IMAGE_VERSION=53 TEST_SCENARIO=mpu_write_bootloader ...` | PASS |
| `make ... SLOT=a IMAGE_VERSION=54 ... all update-package inspect-update-package verify-update-package` | PASS |
| `st-info --probe` | PASS, ID `0x419`, 1 MiB flash, 256 KiB SRAM |
| read-only Option Byte inspection | PASS, RDP0 / WRP open |
| `make PYTHON=.venv/bin/python test-fast` | PASS, 161 Python tests, C host suites and deterministic fuzz smoke |
| `make PYTHON=.venv/bin/python test-security` | PASS, host/security profile; optional external analyzers absent |
| `make PYTHON=.venv/bin/python sanitize` | PASS, ASan/UBSan host and fuzz smoke |
| `python3 tools/check_documentation.py --output ...` | PASS, 0 missing local Markdown links |
| `python3 tools/check_workflows.py --output ...` | PASS, 4 workflows structurally valid offline; not run on GitHub |
| `python3 tools/check_deterministic_build.py` | PASS, identical hashes in the two temporary clean checkouts |
| `python3 tools/publication_scan.py --tracked --output ...` | CLEAN for error-level findings; 369 warning-level historical paths remain for publication review |
| `git diff --check` | PASS after final documentation edits |
| Part 5 selected HIL corruption run | INCONCLUSIVE; flash-loader failure before selected test execution, restore verified |

The publication scan's warning set includes six tracked logic-analyzer captures,
historical raw-log directories, option-byte command examples and local device
names in historical evidence. Device names in the current public-facing
documentation use `/dev/ttyUSBx`; the historical artifacts must be explicitly
reviewed or removed from a future public release branch before publication.

The complete Part 1-4 host, fuzzing, coverage, sanitizer, release and CI
results remain documented in their existing reports. Remote GitHub Actions
were not run for this unpushed branch.

## Findings

| Classification | Finding | Action |
| --- | --- | --- |
| MEDIUM | Rejected candidate version was exposed as running firmware identity | Fixed telemetry and confirmation snapshots; host and hardware regression added |
| TEST-INFRASTRUCTURE | ST-Link flash loader failed while attempting HIL test-image programming with IWDG-enabled target | Campaign stopped; restore verified; no claim of current HIL matrix pass |
| INCONCLUSIVE | Full physical power-loss evidence unavailable | Requires safe external supply control; remains RDP2 blocker |
| LIMITED | Current MPU test did not retain independently readable fault context | Documented as limited evidence; no overclaim |

## Verbleibende Risiken

- No external security review, invasive analysis or side-channel analysis.
- Only a limited number of real boards has been tested.
- Rollback state is software-backed and has no hardware monotonic counter.
- The root public key is fixed in Stage 0; compromise or loss of the private
  signing key requires the documented recovery decision and may be final after
  RDP2.
- Power-loss behavior is not hardware validated.
- The HIL flash programming path can interact badly with an active IWDG.
- WRP is not activated and RDP2 is disabled.
- No long-duration environmental or production qualification was performed.
- Remote CI and external-builder reproduction remain unexecuted.

## Finale Bewertung

| Category | Score / 10 | Reason |
| --- | ---: | --- |
| Architektur | 8 | Clear Stage-0/A-B/update separation; no production qualification |
| Secure Boot | 8 | Live signed boot/update and prior HIL rejection evidence |
| Updatepfad | 8 | UART A/B updates and negative authorization evidence |
| Recovery | 7 | Fallback and signed recovery exist; full physical recovery open |
| Rollback | 8 | Live lower/same-version rejection; software floor limitation |
| Watchdog | 8 | IWDG trial reset and fallback evidence |
| Metadatenrobustheit | 7 | Redundancy and prior corruption HIL evidence; final rerun incomplete |
| Power-Loss-Robustheit | 2 | No physical campaign |
| Root of Trust | 8 | Fixed key and signed chain; no hardware counter |
| Schlüsselmanagement | 7 | Fingerprint and procedures documented; custody not audited |
| MPU | 7 | Enabled and normal-path validated; fault context incomplete |
| Runtime Security Monitor | 8 | Stable public UART schema and vector evidence |
| Parserrobustheit | 8 | Strong prior host/fuzz evidence |
| Tests | 8 | Broad prior suite plus current regression |
| Fuzzing | 8 | Existing limited campaigns; not a proof |
| CI | 6 | Workflows locally validated, not remotely executed |
| Reproduzierbarkeit | 8 | Local reproducible tooling and hash checks |
| Releaseprozess | 8 | Local candidate, SBOM, provenance and verification |
| Dokumentation | 9 | Extensive, evidence-labeled documentation |
| Open-Source-Reife | 9 | Community, release and audit guidance present |
| Hardwareevidenz | 7 | Real signed updates/fallbacks; important gaps remain |
| RDP2-Bereitschaft | 2 | Explicit no-go due power-loss and owner/readiness blockers |
| Produktionsnähe | 4 | Research platform, not qualified or externally reviewed |

Scores above 9/10 were not assigned.

## Sicherheitsbestätigung

- RDP2 was not activated.
- No automatic RDP2 write command was executed.
- No WRP write test was performed.
- No public push, tag or GitHub release was created.
- No physical power-loss campaign was performed.

**FINAL HARDWARE VALIDATION INCOMPLETE**
