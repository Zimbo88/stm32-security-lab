# Final hardware baseline

This is the read-only baseline captured before the Part 5 hardware campaign.
It describes the board attached to the local laboratory host on 2026-07-31.
Raw captures and flash images are stored only in the ignored local directory
`baseline/final-hardware-campaign/`.

## Repository

- Branch before campaign: `chore/open-source-release-readiness`
- Part 5 branch: `test/final-hardware-validation`
- Baseline commit: `c9de8a5bb2ec16db84d9e2f517d5aff5db840956`
- Tags observed: `v1.0.0`, `v1.0.1`, `v1.0.2`; no tag was changed.
- The Part 1–4 commits are ancestors of the baseline commit.
- The worktree was clean before creating the Part 5 branch.

## Board and connections

| Item | Observation |
| --- | --- |
| MCU | STM32F42x/F43x, STM32F429IGT6-class target |
| Device ID | `0x419` |
| Flash | 1 MiB |
| Device SRAM | 256 KiB; project application profile uses 128 KiB |
| ST-Link | ST-LINK/V2, firmware `V2J37S7` |
| Target voltage | 3.187377 V reported by the probe |
| UART | `/dev/ttyUSB0` locally, FTDI USB-UART, USART1 |
| UART settings | 115200 baud, 8N1, 3.3 V TTL |
| UART pins | PA9 TX, PA10 RX, common GND |
| Reset | NRST is not connected; `st-flash reset` reports an AIRCR software reset |
| RDP | Level 0 |
| WRP | All sectors unprotected according to `OPTCR=0x0fffaaed` |

The ST-Link serial number and host-specific identifiers are intentionally not
recorded here. The public report uses only the anonymized local device label
`stm32f429-board-local-01`.

## Persistent flash baseline

The complete 1 MiB flash was read with `st-flash read`; no flash write or mass
erase was performed while taking this baseline. The full-image SHA-256 is:

```text
a49a8100a6deaa4fd9467a85a5cf6ae0ae3b972096fa77f65ca434ec3338ef8d
```

The project layout is:

| Region | Address | Size | SHA-256 |
| --- | ---: | ---: | --- |
| Stage 0 bootloader | `0x08000000` | `0x8000` | `679e88fbdc215e42c0c9637be5730b0cd04d744d2ce3ecc5cf1b5cab2fa1ba9a` |
| Metadata A | `0x08008000` | `0x4000` | `6b9113c4b06863eff7293359ec7104ca316ab5fc7fa8ae1778452de920c0b4af` |
| Metadata B | `0x0800c000` | `0x4000` | `ed61aaeb415e2f11056a8ac37a5a994fc422af21a5c24ac4eb52fa8a42fe331d` |
| Update metadata | `0x08010000` | `0x10000` | `71189f7fb6aed638640078fba3a35fda6c39c8962e74dcc75935aac948da9063` |
| Slot A | `0x08020000` | `0x60000` | `19bccf5707c9eeacdf140a3bfcd2a09d5dc6d445d80c1acb3569a21cbe3a45d7` |
| Slot B | `0x08080000` | `0x60000` | `d100241788b09fd29637951bfa9d8243931e83a88c901c25db33f59c33201eef` |
| Recovery | `0x080e0000` | `0x20000` | `b5a41c3758763bbec72769fab4a2533bf2db0b6312d93d25a695f9e4b9e02260` |

The raw files and the machine-readable manifest are local-only artifacts:
`baseline/final-hardware-campaign/baseline-manifest.json`.

## Current boot state

The two decoded metadata records were both valid, including CRC, zero padding,
and the commit markers:

| Copy | Sequence | State | Active | Candidate | Version | Attempts | Confirmation |
| --- | ---: | --- | --- | --- | ---: | ---: | ---: |
| A | 111 | `PENDING_TRIAL` | A | B | 46 | 2 | 0 |
| B | 112 | `CONFIRMED` | B | none | 46 | 0 | 1 |

The newest valid record is copy B. The observed boot state is therefore
confirmed Slot B, version 46, with rollback floor 46. The older pending record
is retained as historical metadata and is not selected.

The live signed images were extracted from the flash readback and verified
offline with the public key embedded in Stage 0:

| Slot | Image version | Vector | Signature | Payload hash |
| --- | ---: | ---: | --- | --- |
| A | 45 | `0x08020200` | valid | valid |
| B | 46 | `0x08080200` | valid | valid |

The embedded public-key fingerprint is SHA-256:

```text
482dd9daac3d406f779995a50a00eb2ac9948eb412cba4403e2f81091780499a
```

## Read-only command evidence

The following commands were executed during the baseline. They do not write
flash or option bytes:

```sh
st-info --probe
st-flash read /tmp/<temporary>/flash.bin 0x08000000 0x00100000
openocd -f interface/stlink.cfg -f target/stm32f4x.cfg \
  -c 'init; stm32f2x options_read 0; shutdown'
st-flash reset
PYTHONPATH=tools python3 -m stm32ctl --port /dev/ttyUSB0 --baud 115200 --json info
PYTHONPATH=tools python3 -m stm32ctl --port /dev/ttyUSB0 --baud 115200 --json status
```

After a fresh reset, `stm32ctl info` returned protocol version 1, a 1024-byte
maximum payload, and session state 0. `status` returned an idle installer with
zero accepted bytes and zero programmed blocks. A direct UART capture during a
reset contained no human-readable application telemetry; the current firmware
uses the structured bootloader protocol for the observed interaction.

## Available update artifacts

The current board floor is version 46. The highest locally available package
that can be inspected is version 8, and the historical device-key packages are
therefore rollback-invalid for this board. The repository also contains
synthetic CI/test-key packages; those are not valid for this board and were not
used as positive hardware updates.

Consequently, a new positive A-to-B or B-to-A update cannot be claimed in this
campaign unless an already available package signed by the embedded device key
and newer than version 46 is found. A private key must not be reconstructed or
copied for this purpose.

## Initial campaign limits

- RDP2 was not activated.
- No option byte was written.
- WRP was not activated.
- No power interruption occurred during baseline capture.
- A controllable power-cut device was not identified in the host interfaces at
  baseline time; a power-loss campaign therefore requires an explicitly
  verified manual or programmable supply control before it can be run.
- The baseline is sufficient for reversible experiments only after the local
  full-flash image and readback verification are checked again.

These limits are evidence boundaries, not successful-test claims.
