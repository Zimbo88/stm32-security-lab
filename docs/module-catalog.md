# Module catalog

The catalog helper provides fixed-size CRC-protected records containing
sequence, module identity/version, minimum version, state, slots, failures,
result, and fingerprint. Updates are intended to be append-only and recovery
must select the newest valid record.

`tools/module_catalog.py` rejects invalid record sizes, bad CRCs, nonzero
reserved padding, zero identity fields, and version floors above the current
version. `select_newest` and `select_newest_from_blob` ignore corrupted records
and return the highest-sequence valid record.

EXP070 extends the same append-only catalog approach for atomic module
installation. `tools/module_install.py` uses fixed 128-byte records with CRC32,
sequence numbers, module identity, module version, monotonic minimum accepted
version, state, active and candidate slot IDs, failure count, signer key ID, and
package fingerprint. Recovery scans complete records only, ignores invalid CRCs
and partial trailing records, selects the newest state per module, and separately
tracks the newest confirmed record so an interrupted candidate cannot replace
the last confirmed module.

EXP070 state records accept only predefined slot IDs (`A`, `B`, or none).
Signer revocation records are separate catalog entries and must not carry module
state. See `docs/exp070-atomic-install.md` for the install state machine and
power-loss recovery policy.
