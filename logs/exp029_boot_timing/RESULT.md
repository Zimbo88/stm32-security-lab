# EXP029 – Secure Boot Timing Measurement

## Setup

- Target: STM32F429IGT6
- Logic analyzer: FX2-based analyzer
- Sample rate: 8 MHz
- Channels:
  - D0: NRST
  - D1: USART1 TX / PA9
  - D2: PH12 application heartbeat

## Main capture

- Capture duration: 4.000 s
- Samples: 32,000,000
- Reset pulse width: 306.211 ms
- Reset release to first UART transition: 0.056 ms

## UART result

The bootloader produced the expected authenticated-boot sequence:

- EXP019 ED25519 SIGNED BOOT
- Manifest parsed successfully
- Image version 2 accepted
- Rollback floor version 2 accepted
- SHA-512 and Ed25519 verification completed
- Verification = OK
- Signature and payload hash accepted
- Jumping to application

## PH12 result

A separate five-second PH12 capture produced:

- 2 signal transitions
- 2,249,202 low samples
- 2,750,798 high samples
- Final level high

This confirms that the PH12 signal is electrically valid and changes state after secure boot.

## Conclusion

EXP029 successfully measured the authenticated boot path.

The STM32 begins UART activity approximately 56 microseconds after reset release. The bootloader validates the signed image and transfers control to the application. Independent PH12 acquisition confirms application-side GPIO activity.

Status: PASS
