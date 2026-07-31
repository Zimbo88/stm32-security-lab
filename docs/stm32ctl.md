# stm32ctl

`stm32ctl` is a Python host tool for the EXP045 bootloader UART binary update
protocol. It uses pyserial for the physical UART and the same `SUPD` frame
format documented in `docs/uart-binary-protocol.md`.

From the repository root, run it with the tools directory on `PYTHONPATH`:

```sh
PYTHONPATH=tools python3 -m stm32ctl --port /dev/ttyUSBx info
```

Equivalent from inside `tools/`:

```sh
python3 -m stm32ctl --port /dev/ttyUSBx info
```

## Commands

```sh
python3 -m stm32ctl --port /dev/ttyUSBx info
python3 -m stm32ctl --port /dev/ttyUSBx status
python3 -m stm32ctl --port /dev/ttyUSBx reset
python3 -m stm32ctl --port /dev/ttyUSBx update --package path/to/firmware.update.bin
```

Common options:

- `--baud`: UART baudrate, default `115200`.
- `--timeout`: response timeout in seconds, default `15.0`. Hardware
  `BEGIN_UPDATE` includes candidate-slot erase and may take several seconds.
- `--retries`: retries for idempotent read-only requests only, default `1`.
- `--json`: emit machine-readable JSON.

Update options:

- `--package`: signed update package to stream.
- `--block-size`: payload bytes per `WRITE_BLOCK`; must not exceed the target's
  advertised maximum.
- `--public-key-hex`: 32-byte Ed25519 public key as hex.
- `--public-key-header`: C header containing the Ed25519 public key.
- `--quiet`: suppress progress output.

If no public-key option is supplied, `stm32ctl` verifies the package with
`firmware/exp045_bootloader_v2/src/firmware_public_key.h`.

## Update Safety

Before opening the serial port for `update`, the tool validates the package
locally through the existing `tools/update_package.py` logic:

- update package format version,
- target compatibility,
- image type,
- canonical header padding,
- known slot vector address,
- payload size and package size,
- Ed25519 signature against the configured public key.

The tool contains no private signing key and never signs firmware. The target
still determines the inactive slot and performs rollback, flash readback, hash,
signature and metadata checks.

Retries are deliberately limited. `HELLO`, `GET_INFO` and `GET_STATUS` may be
retried because they are read-only. `BEGIN_UPDATE`, `WRITE_BLOCK`,
`FINISH_UPDATE`, `ABORT_UPDATE` and `RESET` are not retried automatically. If a
`WRITE_BLOCK` ACK is lost, the block is not sent again; the command fails and
the tool attempts a best-effort abort without assuming whether the target
accepted the block.

## Exit Codes

- `0`: success.
- `1`: generic failure or interrupted command.
- `2`: command-line usage error from argparse.
- `3`: serial transport error.
- `4`: timeout.
- `5`: protocol or CRC error.
- `6`: target returned `NACK`.
- `7`: local package validation failed.

## Notes

The binary protocol currently has no `slots` or `metadata` commands. Those are
available only through the separate read-only text diagnostic console, so
`stm32ctl` intentionally implements only `info`, `status`, `update` and
`reset`.
