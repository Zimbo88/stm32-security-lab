# Safety Model

Hardware execution modifies flash only through declared regions:

- bootloader
- metadata A
- metadata B
- Slot A
- Slot B

Before any destructive action, the framework reads every region into
`flash-backup/` and records size, address, SHA-256, and timestamp in
`backup-manifest.json`.

After each test and again at suite exit, the framework writes the original
backup bytes back to flash, reads every region into `restore-readback/`, and
compares SHA-256 hashes. A suite is unsuccessful if any readback differs.

The framework refuses destructive execution when:

- `st-flash` is unavailable,
- the UART device is missing,
- required firmware directories or build outputs are missing,
- configured regions overlap,
- a region exceeds the flash boundary,
- a backup is incomplete,
- the result directory cannot be created.

`--dry-run` validates configuration, renders build commands, and emits a test
plan without touching hardware.

`--keep-test-state` is dangerous because it skips final restoration. It requires
typed interactive confirmation and is not allowed in non-interactive mode.

The framework never configures option bytes, RDP, WRP, OTP, or irreversible
provisioning settings.
