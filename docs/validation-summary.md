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

## Secure update v2 hardware completion

Date: 2026-07-27.

Target evidence was collected on an STM32F429IGT6-class board at RDP Level 0
with ST-Link and USART1 attached. The previous blocking update failure was
reproduced as a host timeout against real target timing: `BEGIN_UPDATE`
requires about 6.3 seconds because the target erases the inactive slot before
ACKing, while the old host default timeout was 1 second. A second measured
failure was a corrupted final ACK when the target reset immediately after
`FINISH_UPDATE`; the UART response now waits for transmission complete before
reset. Hardware then exposed a trial-boot wiring error: boot policy used a
read-only flash backend, so `CANDIDATE_READY` could not be committed to
`PENDING_TRIAL`; the policy now uses the existing metadata-only flash backend.

Validated secure-update observations:

- baseline Slot A confirmed boot selected `CONFIRMED`, ran SHA-512 and
  Ed25519, and jumped to EXP066;
- A-to-B update completed with Slot B version 3, selected `TRIAL`, booted
  EXP066 and became `CONFIRMED`;
- B-to-A update completed with Slot A version 4, selected `TRIAL`, booted
  EXP066 and became `CONFIRMED`;
- Slot A and Slot B readbacks matched the expected signed packages after the
  positive update paths;
- rollback rejection for lower and same versions was observed on hardware;
- corrupted manifest, target, signature and payload packages were rejected
  fail-closed;
- abort and reset during `WRITING` left only the confirmed fallback slot
  bootable;
- UART CRC, sequence, partial-frame and random-byte negative tests did not
  block normal boot;
- final metadata was restored to `CONFIRMED`, active Slot A, version 4;
- Option Bytes remained unchanged at `OPTCR=0x0fffaaed`,
  `OPTCR1=0x0fff0000`.

Physical power-removal and visual LED observations remain separate manual
evidence items; they are not claimed by this UART/ST-Link evidence set.

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
