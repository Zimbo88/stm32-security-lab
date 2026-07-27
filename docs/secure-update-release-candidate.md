# Secure Update Release Candidate

Date: 2026-07-27

Scope: RDP0 hardware-in-the-loop release candidate for the EXP045 secure-update
chain. The secure-boot and dual-slot update campaign has completed on the
documented STM32F429IGT6-class target. RDP2 is not approved by this report.

## Implemented Components

- Streaming update installer with fixed program/readback buffers and no full
  installed-image RAM buffer.
- Candidate verification from memory-mapped candidate slot after flash write.
- UART RX polling transport on USART1, PA9 TX, PA10 RX, 115200 baud.
- Transport-neutral byte reader for host-testable parsers.
- Deterministic binary `SUPD` protocol with CRC32, sequence numbers, ACK/NACK,
  bounded payloads and parser resynchronization.
- Update service and bounded boot entry window before normal secure boot.
- Read-only UART diagnostic console.
- `stm32ctl` Python host tool using pyserial, local package verification,
  sequencing, status parsing and safe retry policy.
- End-to-end audit documentation and hardware-test plan.

## Known Limitations

- UART entry and console timeouts are still poll-budget based. They are bounded
  and tested, but not calibrated milliseconds. The completed RDP0 run measured
  the update path and established a 15 second default host response timeout for
  candidate-slot erase before `BEGIN_UPDATE` ACK.
- No physical update GPIO is selected yet because the repository does not
  document a definitive board pinout.
- `stm32ctl` retries only read-only commands. A lost ACK after the target has
  consumed a read-only frame can still surface as a timeout or sequence error;
  the host does not silently resynchronize by replaying state-changing frames.
- The current binary protocol does not expose `slots` or `metadata`; those are
  available through the read-only diagnostic console.
- Existing deterministic-build tooling uses `git archive HEAD`, so it validates
  committed HEAD. Run it from the exact tree intended for release evidence.
- Positive update `BEGIN_UPDATE` includes candidate-slot erase on hardware. The
  measured STM32F429IGT6 erase/program path requires a multi-second host
  response timeout.

## Validation Results

Release-candidate hardware-readiness run:

- `python3 -m pytest -q tests`: passed.
- Targeted Python checks passed:
  - `python3 -m pytest -q tests/test_release_artifacts.py tests/test_update_package_tool.py tests/test_exp066_prehardware.py`: 20 passed.
- Normal C host tests passed:
  - `make -C tests/update_protocol clean test`
  - `make -C tests/update_storage clean test`
  - `make -C tests/uart clean test`
  - `make -C tests/diagnostic_console clean test`
  - `make -C tests/host_verifier clean test`
  - `make -C tools clean test`
  - `make -C tests/rsm_core clean test`
- Supported ASan/UBSan host tests passed:
  - `tests/update_protocol`
  - `tests/update_storage`
  - `tests/uart`
  - `tests/diagnostic_console`
  - `tests/host_verifier`
  - `tools`
- Bootloader clean build and report passed:
  - `make -C firmware/exp045_bootloader_v2 clean report LAYOUT_PROFILE=stm32f429_1m`
- exp066 clean builds passed:
  - Slot A:
    ```sh
    make -C firmware/exp066_research_platform_core clean verify-signed \
      LAYOUT_PROFILE=stm32f429_1m \
      SIGNING_SEED=../exp065_signed_app/keys/firmware_signing_seed.bin \
      PUBLIC_KEY_HEADER=../exp045_bootloader_v2/src/firmware_public_key.h
    ```
  - Slot B:
    ```sh
    make -C firmware/exp066_research_platform_core slot-b \
      LAYOUT_PROFILE=stm32f429_1m \
      SIGNING_SEED=../exp065_signed_app/keys/firmware_signing_seed.bin \
      PUBLIC_KEY_HEADER=../exp045_bootloader_v2/src/firmware_public_key.h
    ```
- Offline update-package verification passed:
  - Slot A with header key and hex key.
  - Slot B with header key and hex key.
  - Wrong public key rejected with `Ed25519 signature verification failed`.
- `git diff --check`: passed.
- Ruff:
  - `.venv-hil/bin/ruff check tools/check_deterministic_build.py tests/test_release_artifacts.py`: passed.
- Deterministic build:
  - `python3 tools/check_deterministic_build.py`: passed.
  - The comparison normalizes only the volatile `verification_timestamp_utc`
    field in update-package verification reports; firmware binaries, packages,
    package hashes, key fingerprints, slot metadata and verification results
    remain compared.
- Hardware helper script:
  - `bash -n tools/run_secure_update_hardware_test.sh`: passed.
  - `tools/run_secure_update_hardware_test.sh --help`: passed.
- Repository audit:
  - `bash audit/run_repository_audit.sh`: passed and wrote
    `audit/repository-audit.txt`.
- Mypy:
  - `.venv-hil/bin/mypy tools/stm32ctl tests/test_stm32ctl.py`: passed.
- Tool availability:
  - `ruff` and `python3 -m ruff` are not available in the system Python; use
    `.venv-hil/bin/ruff` for local checks.

Hardware completion run, 2026-07-27, STM32F429IGT6, RDP Level 0:

- Root-cause timing for the previous A-to-B failure:
  - HELLO ACK: about 20 ms.
  - `BEGIN_UPDATE` ACK: 6.31 s A-to-B, 6.41 s B-to-A.
  - 512-byte `WRITE_BLOCK` ACKs: about 80 ms each.
  - `FINISH_UPDATE` ACK and verification: about 1.01 s.
- Positive A-to-B update:
  - Slot A version 2 confirmed baseline booted with SHA-512 and Ed25519
    verification enabled.
  - Slot B version 3 update completed with 39 write frames.
  - Bootloader selected Slot B as `TRIAL` after reset.
  - EXP066 booted and confirmed Slot B.
  - Follow-up console metadata showed `CONFIRMED`, active slot B, version 3.
  - Slot B flash readback matched the transferred package byte-for-byte.
- Positive B-to-A update:
  - Slot B version 3 confirmed baseline updated to Slot A version 4.
  - Bootloader selected Slot A as `TRIAL` after reset.
  - EXP066 booted and confirmed Slot A.
  - Follow-up console metadata showed `CONFIRMED`, active slot A, version 4.
  - Slot A flash readback matched the transferred package byte-for-byte.
- Negative hardware update tests passed:
  - lower version rejected with `ROLLBACK`;
  - same version rejected with `ROLLBACK`;
  - wrong slot package rejected with `VERIFY`;
  - corrupted manifest, target field and signature rejected before release;
  - corrupted payload rejected at `FINISH_UPDATE`;
  - abort after first block left only confirmed fallback bootable;
  - reset during `WRITING` left only confirmed fallback bootable;
  - bad CRC and skipped sequence returned `NACK(CRC)` and `NACK(SEQUENCE)`;
  - partial frame timeout and random UART bytes did not block normal boot.
- Final board state restored:
  - metadata `CONFIRMED`, active slot A, version 4;
  - Slot B contains the valid tested version 3 image;
  - final boot selected confirmed Slot A and executed SHA-512 and Ed25519;
  - final Option Bytes remained `OPTCR=0x0fffaaed`,
    `OPTCR1=0x0fff0000`.

## Bootloader Size

Current hardware-validation size after the UART timeout and trial-metadata
fixes:

- Bootloader binary: 29,816 bytes.
- Reserved bootloader region: 32,768 bytes.
- Free reserve: 2,952 bytes.
- EXP066 Slot A application binary: 19,884 bytes.
- EXP066 Slot A update package: 20,396 bytes.
- EXP066 Slot B application binary: 19,876 bytes.
- EXP066 Slot B update package: 20,388 bytes.

Largest linked symbols/modules observed in the final validation:

- `execute_line`: diagnostic console command dispatch, about 1,976 bytes.
- `sha512_compress`: about 1,306 bytes.
- `fe_mul`: about 1,380 bytes.
- `handle_frame`: protocol command dispatcher, about 712 bytes.
- `update_installer_begin`: about 638 bytes.
- Largest BSS object: update service session, about 2,888 bytes.
- Fixed update buffers: 512-byte program buffer and 512-byte readback buffer.

Largest stack users observed:

- `crypto_argon2`: 2,256 bytes, linked from Monocypher but not on the update or
  boot verification path.
- `crypto_eddsa_check_equation`: 1,088 bytes.
- `update_installer_install`: 848 bytes, compatibility wrapper path.
- `crypto_sha512_hkdf_expand`: 448 bytes, not on the update path.
- `ge_scalarmult_base`: 432 bytes.
- `hash_installed_payload`: 312 bytes.

## Hardware Evidence Not Claimed

Physical power removal and visual LED observation remain manual evidence items.
The completed run used ST-Link reset for reset/power-failure class checks and
UART evidence for boot/update state. RDP2 remains blocked until physical
power-loss timing has been repeated with controlled target power.

## RDP2 Blockers

RDP2 is not released. Blockers:

- Power-loss behavior has not been physically verified during erase, program,
  finish, post-`CANDIDATE_READY`, or pre-confirmation phases.
- Recovery procedure under failed updates has not been validated after
  physically removing target power.
- Option Byte policy and irreversible provisioning steps are not documented or
  rehearsed for this chain.

## Historical Implementation Plan

The following plan records how the secure-update chain was split into reviewed
changes before the hardware-validation checkpoint. It is retained as trace
evidence, not as remaining work for the release.

1. `exp066: keep RSM UART output line endings consistent`
   - `firmware/exp066_research_platform_core/src/experiment_telemetry.c`
   - `firmware/exp066_research_platform_core/src/runtime_monitor.c`

2. `secure-update: add streaming installer`
   - `firmware/exp045_bootloader_v2/src/update_installer.c`
   - `firmware/exp045_bootloader_v2/src/update_installer.h`
   - `firmware/exp045_bootloader_v2/src/update_package.c`
   - `firmware/exp045_bootloader_v2/src/update_package.h`
   - installer/update storage host tests
   - `docs/secure-update-streaming-design.md`

3. `bootloader-uart: add polling RX and byte reader`
   - `firmware/exp045_bootloader_v2/src/uart.c`
   - `firmware/exp045_bootloader_v2/src/uart.h`
   - `firmware/exp045_bootloader_v2/src/byte_reader.c`
   - `firmware/exp045_bootloader_v2/src/byte_reader.h`
   - UART host tests
   - `docs/uart-bootloader-transport.md`

4. `secure-update: add UART binary protocol and service`
   - `firmware/exp045_bootloader_v2/src/update_protocol.c`
   - `firmware/exp045_bootloader_v2/src/update_protocol.h`
   - `firmware/exp045_bootloader_v2/src/update_service.c`
   - `firmware/exp045_bootloader_v2/src/update_service.h`
   - protocol/update service host tests
   - `docs/uart-binary-protocol.md`

5. `bootloader: integrate update entry window`
   - `firmware/exp045_bootloader_v2/src/main.c`
   - `firmware/exp045_bootloader_v2/src/update_mode.c`
   - `firmware/exp045_bootloader_v2/src/update_mode.h`
   - `firmware/exp045_bootloader_v2/src/system_reset.c`
   - `firmware/exp045_bootloader_v2/src/system_reset.h`

6. `bootloader: add read-only UART diagnostic console`
   - `firmware/exp045_bootloader_v2/src/diagnostic_console.c`
   - `firmware/exp045_bootloader_v2/src/diagnostic_console.h`
   - diagnostic console tests
   - `docs/uart-diagnostic-console.md`

7. `tools: add stm32ctl UART update client`
   - `requirements.txt`
   - `tools/stm32ctl/*`
   - Python stm32ctl tests
   - `docs/stm32ctl.md`

8. `secure-update: add audit and RC hardware plan`
   - `docs/secure-update-audit.md`
   - `docs/secure-update-hardware-test.md`
   - `docs/secure-update-release-candidate.md`
   - `tools/run_secure_update_hardware_test.sh`

9. `build: fix deterministic exp066 artifact coverage`
   - `tools/check_deterministic_build.py`

10. `secure-update: preserve reset after final ACK failure and trim entry HELLO path`
   - `firmware/exp045_bootloader_v2/src/update_service.c`
   - `firmware/exp045_bootloader_v2/src/update_protocol.c`
   - `firmware/exp045_bootloader_v2/src/update_protocol.h`
   - regression tests in `tests/update_protocol/test_update_protocol.c`

## Recommendation

Go for public Open Source release as an RDP0 hardware-validated research
reference. No-Go for RDP2. RDP2 remains explicitly not approved.
