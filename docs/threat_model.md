# Threat Model

## Security Objective

The bootloader must execute only firmware that is authentic, intact,
compatible, structurally valid, and permitted by the configured version
policy.

## Protected Assets

- Bootloader control flow
- Trusted firmware public key
- Application authenticity
- Application integrity
- Firmware version policy
- Safe transfer of control to the application

## Considered Adversary Capabilities

The adversary may:

- Modify bytes in the stored application payload
- Modify the stored signature
- Replace the complete firmware image
- Sign firmware with an unauthorized key
- Provide an older correctly signed firmware image
- Provide an unsupported manifest format
- Manipulate the initial MSP
- Manipulate the reset vector

## Mitigated Threats

| Threat | Mitigation |
|---|---|
| Payload modification | SHA-512 payload verification |
| Signature modification | Ed25519 signature verification |
| Unauthorized firmware | Embedded trusted public key |
| Rollback attack | Minimum image-version policy |
| Unsupported manifest | Header-version validation |
| Invalid stack pointer | Initial MSP range validation |
| Invalid execution target | Reset-vector validation |
| Partial validation failure | Fail-safe halt before execution |

## Assumptions

The security design assumes:

- The bootloader itself is trusted.
- The trusted public key embedded in the bootloader is correct.
- The private signing seed remains confidential.
- Debug access and flash protection are configured according to the deployment
  threat model.
- The cryptographic implementation behaves according to its specification.
- The MCU executes from trusted internal flash.

## Out of Scope

The current implementation does not claim to protect against:

- Physical invasive attacks
- Side-channel attacks
- Fault-injection attacks
- Compromise of the signing host
- Theft of the private signing seed
- Malicious replacement of an unprotected bootloader
- Secure firmware update transport
- Confidentiality of firmware contents
- Hardware-backed monotonic version counters
- Production key provisioning and rotation

## Residual Risks

The minimum accepted image version is currently a compile-time policy.

If an attacker can replace or downgrade the bootloader itself, the application
version policy can also be changed.

A production deployment should therefore combine secure boot with appropriate
MCU readout protection, write protection, controlled key provisioning, and a
documented recovery strategy.

## Validation Evidence

The following negative tests were performed successfully:

- Modified payload
- Modified signature
- Unauthorized signing key
- Rollback image
- Unsupported header version
- Invalid initial MSP
- Invalid reset vector

Detailed results are recorded in `docs/secure_boot_validation.md`.
