# EXP070 atomic module installation and rollback

EXP070 implements a host-side model of a power-loss-safe A/B module
installation system. It does not flash hardware and exposes only module-slot
abstractions, never arbitrary Flash addresses.

## Persistent layout

Each installable module has two predefined slots: slot A and slot B. The active
confirmed slot is never overwritten during installation. A candidate is written
to the other slot and becomes bootable only after package verification and an
append-only catalog update.

The catalog is a stream of fixed 128-byte CRC-protected records. Corrupted
records and partial trailing records are ignored during recovery. Record kinds:

- state records for module lifecycle and slot pointers,
- signer revocation records.

Each state record includes sequence, module ID, module version, monotonic
minimum accepted version, state, active slot, candidate slot, failure count,
result, signer key ID, and package fingerprint.
State records accept only slot A, slot B, or no slot; malformed slot IDs fail
closed during decode. Sequence zero is invalid, and attempts to append after
the maximum uint32 sequence fail closed rather than wrapping.

## State machine

EXP070 uses these states:

- `EMPTY`
- `RECEIVING`
- `VERIFIED`
- `PENDING`
- `ACTIVE`
- `CONFIRMED`
- `REJECTED`
- `QUARANTINED`

Install writes a candidate package to the inactive slot and appends
`RECEIVING` then `VERIFIED`. Activation appends `PENDING` then `ACTIVE`.
Confirmation appends `CONFIRMED` and raises the monotonic minimum accepted
version. Failed pending or active candidates append `REJECTED` or
`QUARANTINED` and keep the previous confirmed module available.

## Recovery algorithm

Recovery scans the append-only catalog, verifies each record CRC, ignores
corrupt records and partial tails, selects the highest-sequence state record per
module, and separately tracks the newest confirmed record per module. Pending or
active but unconfirmed candidates do not replace the last confirmed module after
reset. The last confirmed module remains available after simulated interruption
of install, activation, confirmation, and rollback writes.

## Verification policy

Before a candidate is marked verified, the installer checks:

- EXP067 package hash and Ed25519 signature,
- module ID where required,
- signer identity,
- ABI version,
- platform version,
- supported capabilities only,
- rollback floor,
- signer revocation state.

Package fingerprints are tracked in catalog records. The monotonic minimum
accepted version is raised only after confirmation, so a failed pending upgrade
cannot block restoration of the previous confirmed module. New installations
must advance beyond the confirmed version floor; equal-version replay and lower
version downgrade attempts are rejected.

## Key policy

Firmware signing and module signing are separate. The host installer rejects a
module signer whose key ID matches the configured firmware signer key ID.
Signer revocation records reject future installation attempts by revoked module
signers while preserving already confirmed slot availability.

## Host tool

`tools/module_install.py` operates on a JSON-serialized simulated Flash image.
It provides:

- `list`
- `inspect`
- `install`
- `verify`
- `activate`
- `confirm`
- `rollback`
- `quarantine`
- `remove-candidate`
- `revoke-signer`
- `catalog-recovery`

## Threat model and limitations

EXP070 models persistent-state safety and policy behavior on the host. It does
not perform real Flash erase/program timing, hardware brownout testing, MPU
configuration, watchdog recovery, or boot-time module execution. Hardware power
loss must still be validated on expendable boards before any irreversible
provisioning.
