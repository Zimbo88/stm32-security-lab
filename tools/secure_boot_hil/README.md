# STM32 Secure Boot HIL

`secure-boot-hil` is the hardware-in-the-loop validation framework for the
STM32F429 secure-boot stack in this repository.

It builds the EXP045 bootloader and EXP066 Slot A/Slot B update packages,
caches immutable artifacts per run, generates deterministic fault images,
backs up all test-relevant flash regions, executes st-flash/UART test cases,
and restores the original flash before reporting a suite result.

Hardware execution is destructive to the declared flash regions during the run.
The default transaction restores the original bootloader, metadata A,
metadata B, Slot A, and Slot B and verifies each restored region by readback.

## Install

```bash
python3 -m venv .venv-hil
. .venv-hil/bin/activate
python -m pip install --upgrade pip
python -m pip install -e tools/secure_boot_hil[dev]
```

## Host Checks

```bash
tools/secure_boot_hil/scripts/run_host_checks.sh
```

Use the `PYTHON` environment variable when the checks should run inside a
specific virtual environment:

```bash
PYTHON="$PWD/.venv-hil/bin/python" tools/secure_boot_hil/scripts/run_host_checks.sh
```

## Dry Run

```bash
secure-boot-hil run \
  --repo-root ~/stm32-security-lab \
  --uart /dev/ttyUSBx \
  --baud 115200 \
  --capture-seconds 5 \
  --reset-cycles 10 \
  --dry-run \
  --non-interactive
```

## Hardware Run

```bash
secure-boot-hil run \
  --repo-root ~/stm32-security-lab \
  --uart /dev/ttyUSBx \
  --baud 115200 \
  --capture-seconds 5 \
  --reset-cycles 10 \
  --require-confirmation
```

The framework uses `st-flash` only. It does not configure option bytes, RDP,
WRP, OTP, or any irreversible protection setting.

## Reports

Each run directory contains JSON, CSV, Markdown, HTML, JUnit XML, UART evidence,
backup artifacts, restore readbacks, cached images, and mutation metadata.

```bash
secure-boot-hil report hil-results/run-YYYYMMDDTHHMMSSZ
secure-boot-hil inspect-backup hil-results/run-YYYYMMDDTHHMMSSZ
```
