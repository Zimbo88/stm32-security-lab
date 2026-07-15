# EXP019 – Ed25519 signed bootloader

This bootloader verifies:

1. signed-image metadata and bounds;
2. SHA-512 of the application payload;
3. Ed25519 signature over the fixed 96-byte manifest;
4. application vector table and initial MSP.

Flash layout:

- 0x08000000–0x08007FFF: bootloader (sectors 0 and 1, max 32 KiB)
- 0x08008000: signed image header
- 0x08008200: application vector table and payload

The private signing key is not included.
