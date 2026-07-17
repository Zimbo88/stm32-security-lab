# EXP031 – End-to-End Security Validation

## Platform

Target:
- STM32F429IGT6

Boot chain:
- Bare-metal startup
- UART diagnostics
- SHA-512 image hashing
- Ed25519 signature verification
- Rollback protection
- Flash write protection
- RDP Level 1
- Recovery image support

---

## Validation Summary

| Test | Result |
|-------|--------|
| Bare-metal startup | PASS |
| UART diagnostics | PASS |
| CRC bootloader | PASS |
| Signed image verification | PASS |
| SHA-512 image hash | PASS |
| Ed25519 signature verification | PASS |
| Rollback protection | PASS |
| Manifest validation | PASS |
| Flash write protection | PASS |
| RDP Level 1 | PASS |
| Recovery package | PASS |
| Boot timing measurement | PASS |
| Reset fault injection | PASS |

---

## Boot Sequence

Reset

↓

Bootloader

↓

Manifest parsing

↓

SHA-512

↓

Ed25519 verification

↓

Version check

↓

Jump to application

↓

Verified application

---

## Reset Fault Injection

22 boot attempts

18 interrupted boot attempts

4 complete boot sequences

0 authentication bypasses

Result:

PASS

No application execution without successful authentication.

---

## Security Assessment

The bootloader consistently rejected incomplete authentication.

Repeated reset injection during SHA-512 and Ed25519 verification did not
produce any authentication bypass.

Flash protection and rollback protection remained active.

The verified application executed only after successful image
authentication.

---

## Overall Result

STATUS: PASS

The secure boot chain fulfills the intended security objectives for this
project.
