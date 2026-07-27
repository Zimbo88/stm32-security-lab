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
- Provide a signed update package for the wrong slot
- Interrupt or abort an update before completion
- Send malformed UART update frames
- Provide an unsupported manifest format
- Provide noncanonical manifest flags or reserved fields
- Provide malformed payload ranges or address-overflow cases
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
| Noncanonical manifest metadata | Flags and reserved-field validation |
| Malformed payload range | Size, flash-region, and overflow validation |
| Wrong update slot | Slot-bound manifest vector address and inactive-slot policy |
| Partial update | Metadata states and final `CANDIDATE_READY` commit rule |
| Malformed UART frame | Bounded parser, length checks, CRC32, sequence checks |
| Invalid stack pointer | Initial MSP range and alignment validation |
| Invalid execution target | Reset-vector validation |
| Handoff context drift | Redundant pre-jump validation and flash vector re-read |
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
- Production remote-update authorization beyond signed firmware packages
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
- Unsupported flags
- Nonzero reserved fields
- Address overflow
- Invalid initial MSP
- Invalid reset vector
- Payload and vector boundary values
- Final jump-context validation

Detailed results are recorded in `docs/secure_boot_validation.md`.
