# Secure Boot Validation Report

## Overview

This document summarizes the manual validation of the EXP045 Bootloader V2
using the EXP066 Research Platform on the STM32F429.

## Test Configuration

- Bootloader address: `0x08000000`
- Signed image address: `0x08008000`
- Manifest address: `0x08008000`
- Signature address: `0x08008060`
- Application vector table: `0x08008200`
- Header version: `1`
- Minimum image version: `2`
- Payload hash: SHA-512
- Signature algorithm: Ed25519

## Validation Summary

| Test | Result |
|------|--------|
| Valid signed image | PASS |
| Modified payload | PASS |
| Modified signature | PASS |
| Wrong signing key | PASS |
| Rollback protection | PASS |
| Header version validation | PASS |
| Initial MSP validation | PASS |
| Reset vector validation | PASS |
| Valid image restored | PASS |

## Valid Signed Image

```text
Verification     = OK
Signature and payload hash accepted.
Jumping to application...
```

## Payload Integrity Validation

```text
Verification     = PAYLOAD SHA512 MISMATCH
Application will NOT be started.
Bootloader halted safely.
```

## Signature Validation

```text
Verification     = ED25519 SIGNATURE INVALID
Application will NOT be started.
Bootloader halted safely.
```

## Unauthorized Signing Key

```text
Verification     = ED25519 SIGNATURE INVALID
Application will NOT be started.
Bootloader halted safely.
```

## Rollback Protection

```text
Image version    = 1
Minimum version  = 2
Verification     = ROLLBACK VERSION REJECTED
Application will NOT be started.
Bootloader halted safely.
```

## Manifest Header Validation

```text
Header version   = 2
Verification     = BAD HEADER VERSION
Application will NOT be started.
Bootloader halted safely.
```

## Initial MSP Validation

The initial MSP was changed from `0x20020000` to `0x10000000`.

```text
Verification     = BAD INITIAL MSP
Application will NOT be started.
Bootloader halted safely.
```

## Reset Vector Validation

The reset vector was changed from `0x08008241` to `0x08000001`.

```text
Verification     = BAD RESET VECTOR
Application will NOT be started.
Bootloader halted safely.
```

## Signing Tool Protection

```text
Initial MSP is outside SRAM: 0x10000000
```

## Recovery Status

```text
RECOVERY_POLICY_UNAVAILABLE
```

## Conclusion

The secure boot implementation successfully rejects:

- Modified firmware payloads
- Invalid Ed25519 signatures
- Unauthorized signing keys
- Rollback images
- Unsupported manifest versions
- Invalid initial stack pointers
- Invalid reset vectors

After each negative test, the original signed firmware was restored and booted successfully.
