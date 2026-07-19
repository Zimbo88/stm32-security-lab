# Architecture

The HIL framework is a Python package that orchestrates the existing STM32
secure-boot implementation without changing firmware trust decisions.

## Components

- `config.py` loads typed configuration and derives flash regions from the
  repository memory-layout generator.
- `build.py` invokes the existing Makefiles and update-package tooling.
- `images.py` parses the repository signed-image manifest and creates immutable
  deterministic mutations from cached valid images.
- `flash.py` renders and runs region-bounded `st-flash` commands.
- `uart.py` captures UART output and extracts complete boot frames from the
  bootloader banner.
- `assertions.py` evaluates literal, regex, and parsed-field assertions.
- `backup.py` and `restore.py` implement the hardware transaction boundary.
- `testspec.py` defines stable test cases as typed data.
- `runner.py` connects build, flash, UART, assertions, restoration, and reports.
- `reporting.py` writes JSON, CSV, Markdown, HTML, JUnit, and performance data.

## Build Flow

Slot A and Slot B use a shared application build directory. The framework
therefore enforces this sequence:

1. Build Slot A.
2. Verify the Slot A package exists and is non-empty.
3. Copy Slot A into the run-specific `image-cache/`.
4. Build Slot B.
5. Copy Slot B into the same run-specific `image-cache/`.

No later step reads Slot A from the application build directory.

## Trust Boundary

The framework never makes an image trusted. Bootability is still decided by the
existing Stage-0 policy, signed-image verifier, metadata recovery logic, and
application confirmation policy.

HIL assertions evaluate observed behavior. They do not bypass signatures, hashes,
rollback checks, metadata validation, or vector-table validation.
