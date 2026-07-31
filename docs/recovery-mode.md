# Signed UART recovery mode

Status: IMPLEMENTED and host tested. The current hardware validation is
documented separately. No physical recovery pin is selected in this layout.

Stage-0 enters recovery when metadata cannot be recovered, no slot is
bootable, the confirmed image fails verification, or trial fallback has no
verified active slot. It prints:

```text
RECOVERY ENTER reason=boot-policy-failure
RECOVERY READY signed-uart-update=required slot=A-bootstrap
```

The mode loops on the existing binary UART update protocol. A text diagnostic
console remains read-only. A successful signed update causes a controlled
reset; a failed or incomplete session remains in recovery and does not jump to
an unverified address.

Recovery accepts only a package that passes all existing checks:

- protocol framing, sequence, length and CRC checks;
- STM32F429 target, application image type, slot address and capacity checks;
- Ed25519 signature and embedded public-key check;
- payload hash and installed-image readback verification;
- monotonic image-version policy;
- metadata commit after `WRITING`, erase/program, verification and
  `CANDIDATE_READY`.

For an unrecoverable metadata journal, recovery bootstrap is deliberately
limited to a signed Slot-A package. It erases only metadata sectors A/B,
publishes `WRITING`, writes Slot A, and then uses the normal trial/confirmation
path. Stage-0, the recovery region and arbitrary memory are not writable by
this path. An ambiguous journal is not silently guessed; the explicit recovery
bootstrap is the only code path allowed to reinitialize its metadata sectors.

Host commands:

```bash
PYTHONPATH=tools python3 -m stm32ctl --port /dev/ttyUSBx info
PYTHONPATH=tools python3 -m stm32ctl --port /dev/ttyUSBx status
PYTHONPATH=tools python3 -m stm32ctl --port /dev/ttyUSBx recovery \
  --package path/to/signed-slot-a-update.bin \
  --public-key-header firmware/exp045_bootloader_v2/src/firmware_public_key.h
```

The `recovery` command is an explicit host-side label for the same signed
protocol. It does not bypass signature, version, address or target-slot rules.
It requires Slot A so that an empty device has one deterministic bootstrap
choice. Normal A/B updates continue to use `stm32ctl update`.

An explicit local recovery request is also available through the read-only
Stage-0 text console: send `recovery` during the UART entry window. Stage-0
then prints `RECOVERY READY` and accepts only the same signed Slot-A bootstrap.
This command changes no metadata and cannot write arbitrary memory.

No SWD, JTAG, ST-Link flash access, GDB, OpenOCD or ROM bootloader is required
after Stage-0 and a valid UART-capable application are provisioned.
