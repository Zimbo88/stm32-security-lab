# EXP022 – Rollback floor demonstration

This bootloader adds a compiled minimum accepted image version:

- `MIN_IMAGE_VERSION = 2`
- correctly signed image version 1: rejected
- correctly signed image version 2: accepted

This is a laboratory policy demonstration, not complete production rollback
protection. An attacker who can replace the bootloader could restore an older
minimum-version policy. Production designs need protected immutable boot code
and trusted monotonic state.
