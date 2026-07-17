# Module catalog

The catalog helper provides fixed-size CRC-protected records containing
sequence, module identity/version, minimum version, state, slots, failures,
result, and fingerprint. Updates are intended to be append-only and recovery
must select the newest valid record.

`tools/module_catalog.py` rejects invalid record sizes, bad CRCs, nonzero
reserved padding, zero identity fields, and version floors above the current
version. `select_newest` and `select_newest_from_blob` ignore corrupted records
and return the highest-sequence valid record.
