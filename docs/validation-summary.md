# Validation Summary

## Hardware validation status

The secure-boot implementation has been validated on real STM32F429 hardware
using the project's hardware-in-the-loop framework.

## Final campaign result

| Category | Result |
|---|---:|
| PASS | 15 |
| FAIL | 0 |
| ERROR | 0 |
| SKIP | 0 |
| Observations recorded | 16 |
| Restore verified | Yes |

## Validated areas

### Payload integrity

- valid payload accepted;
- modified payload rejected;
- hash mismatch rejected;
- application vector-table prefix preserved by mutation tests.

### Authentication

- valid Ed25519 signature accepted;
- invalid signature rejected;
- unauthorized image rejected.

### Manifest validation

Negative tests cover malformed or inconsistent manifest fields, including
unsafe payload ranges and invalid metadata.

### Restoration

The HIL framework backed up and subsequently verified restoration of:

- the bootloader;
- metadata copy A;
- metadata copy B;
- application slot A;
- application slot B.

## Infrastructure failures

Repeated flashing may expose transient ST-LINK, USB, target-state, or flash
loader failures. These are classified as test-infrastructure failures rather
than secure-boot failures when the target can subsequently be flashed and the
firmware behavior is unchanged.

## Evidence policy

Complete local HIL run directories are not committed. Public releases should
contain only curated, anonymized, reproducible evidence required to support
the reported result.
