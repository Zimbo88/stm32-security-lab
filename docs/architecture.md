# Secure Boot Architecture

## Overview

The STM32F429 security lab separates the trusted bootloader from the research
application.

The bootloader is responsible for validating a signed firmware image before
transferring control to the application.

## Trust Boundary

The bootloader is part of the trusted computing base.

The application is treated as untrusted until all configured checks succeed.

The trusted public key is compiled into the bootloader. The corresponding
private signing seed is used only by the host-side signing process.

## Boot Flow

1. MCU reset
2. Bootloader initialization
3. Reset-cause collection
4. Boot-mode evaluation
5. Recovery-policy evaluation
6. Manifest parsing
7. Manifest magic validation
8. Manifest header-version validation
9. Manifest flags and reserved-field validation
10. Image-version policy validation
11. Application range, size, and overflow validation
12. Initial MSP validation
13. Reset-vector validation
14. SHA-512 payload verification
15. Ed25519 signature verification
16. Vector-table relocation
17. Transfer of control to the application

If any validation step fails, the application is not started and the
bootloader halts safely.

## Cryptographic Design

The application payload is protected by a SHA-512 digest.

The signed manifest is authenticated using Ed25519.

The bootloader accepts an image only when the payload hash matches and the
manifest signature is valid under the embedded public key.

The signature covers the serialized manifest. The payload hash inside that
manifest binds the application bytes to the signed metadata.

## Version Policy

The bootloader enforces a minimum accepted image version.

A correctly signed image is still rejected when its image version is lower
than the configured minimum version.

This provides rollback protection against installation of an older but
otherwise authentic firmware image.

## Vector Validation

Before execution, the bootloader validates:

- The initial MSP is inside the supported application SRAM range.
- The initial MSP is 8-byte aligned.
- The reset vector points into the accepted payload.
- The reset vector contains the required Thumb-state bit.

These checks reduce the risk of transferring control to invalid memory.

## Recovery Policy

The current implementation contains a recovery-policy abstraction.

Physical recovery input and firmware-update transport are not implemented yet.
A recovery request therefore resolves to a safe unavailable state.

## Fail-Safe Behaviour

The bootloader uses a deny-by-default policy.

Any malformed, unsupported, outdated, corrupted, incorrectly signed, or
structurally invalid image is rejected before execution.
