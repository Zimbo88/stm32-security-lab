# Quickstart

This is the supported host-only setup for a fresh Linux checkout. Hardware is
optional for the first stages.

## Prerequisites

- Linux with Git, Make and a POSIX shell;
- Python 3.12 (3.11 or newer is supported by the HIL package);
- GCC for host tests;
- `arm-none-eabi-gcc` 13.2.1 or a compatible newer ARM GNU toolchain for
  firmware builds;
- `python3-venv` and a working network connection for the initial dependency
  installation.

Optional hardware tools are `st-info`, `st-flash`, OpenOCD and a separate
3.3 V USB-UART adapter. They are not needed for host tests.

## Host-only setup

```bash
git clone https://github.com/Zimbo88/stm32-security-lab.git
cd stm32-security-lab
python3 -m venv .venv
. .venv/bin/activate
python -m pip install --upgrade pip
python -m pip install -r requirements-security.txt
make test-fast PYTHON=python
```

The complete local security profile is:

```bash
make test-security PYTHON=python
```

It includes the host tests, sanitizer fuzz smoke tests, bounded campaigns,
coverage collection, GCC analysis, Python quality checks, mutation smoke and
the private-key scan. Long fuzzing is separate:

```bash
make fuzz PYTHON=python
```

## Firmware build

```bash
make -C firmware/exp045_bootloader_v2 clean all report \
  LAYOUT_PROFILE=stm32f429_1m
make -C firmware/exp066_research_platform_core clean all SLOT=a \
  LAYOUT_PROFILE=stm32f429_1m
make -C firmware/exp066_research_platform_core clean all SLOT=b \
  LAYOUT_PROFILE=stm32f429_1m
```

Builds are unsigned until an explicit seed is supplied. A local candidate can
use the clearly marked deterministic CI key:

```bash
make release-candidate RELEASE_TEST_KEY=1
make verify-release RELEASE_DIR=dist/v1.1.0-rc1-local
```

This creates a local, test-only candidate. It is not a Git tag, GitHub release
or production artifact. For a research or production-like key, pass an
external file with `RELEASE_SIGNING_SEED=/secure/path/key.seed`; the path is
never copied into the repository or candidate directory.

## Hardware connection

The validated board uses an STM32F429IGT6-class MCU, 1 MiB internal flash,
256 KiB device SRAM, USART1 at 115200 8N1 and 3.3 V TTL:

```text
PA9  USART1_TX  -> USB-UART RXD
PA10 USART1_RX  <- USB-UART TXD
GND             -> USB-UART GND
```

Use a separate USB-UART adapter and do not apply 5 V to the MCU pins. After a
safe reset, an informational handshake is:

```bash
PYTHONPATH=tools python -m stm32ctl --port /dev/ttyUSBx --baud 115200 info
```

ST-Link/SWD is a development and diagnosis interface, not a required release
or post-RDP2 recovery assumption. Follow the hardware procedures before any
flash-changing test.

## Safety

Never commit private signing material, dumps, raw hardware logs or local
device identifiers. RDP2 is irreversible on the target STM32F429. This
repository contains no automatic Option-Byte, RDP or WRP activation.
