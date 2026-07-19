# Project Motivation

## Personal motivation

I started this repository as a personal embedded security research project.

My goal was not only to build a secure boot chain, but to understand every
step involved, verify it on real hardware and document the complete process.

Over time, the repository evolved from small experiments into a structured
research platform covering secure boot, authenticated firmware updates,
rollback protection and hardware-in-the-loop validation.

If this repository helps other embedded developers understand these topics,
then it has achieved its purpose.

## Research objective

STM32 Security Lab was created to establish a controlled and reproducible
baseline for defensive microcontroller security research.

The immediate engineering objective was to implement and validate a complete
secure-boot chain on real STM32F429 hardware. The broader research objective is
to understand how firmware authenticity, boot decisions, flash contents,
protection state, debugger access, reset behavior, and fault conditions
interact on the device.

## Why build a secure baseline first?

Fault injection, glitching, malformed-image loading, protection experiments,
and invasive debugging produce meaningful results only when the expected
behavior is well defined.

A known secure-boot baseline provides measurable reference points:

- which image should be accepted;
- which image should be rejected;
- what memory should remain unchanged;
- which metadata copy should be selected;
- when recovery should occur;
- what UART and debugger output should be observed;
- whether a test altered flash contents or protection state.

Without this baseline, it is difficult to distinguish a successful security
bypass from an unrelated firmware defect, transport failure, or corrupted test
environment.

## Why STM32F429?

The STM32F429 family offers a useful combination of:

- Cortex-M4 architecture;
- substantial internal flash and SRAM;
- documented boot and debug behavior;
- common SWD tooling;
- configurable flash protection;
- broad availability of development boards;
- enough resources for a realistic authenticated boot chain.

The first implementation uses an STM32F429IGT6-class target to understand the
family architecture and establish robust tooling. The methods are intended to
support later investigation of closely related STM32F429 devices, including
the STM32F429VET6 target that motivated the wider project.

## Long-term research direction

Future work may include controlled evaluation of:

- voltage and clock fault injection;
- glitch timing around authentication and flash operations;
- malformed or unauthorized firmware loading paths;
- protection-state changes;
- read-out protection behavior;
- debug and CoreSight accessibility;
- boot and reset state transitions;
- memory changes caused by security configuration;
- behavior observable electrically at and around the microcontroller.

These experiments are intended for owned or explicitly authorized hardware
and should be conducted with recoverable baselines and documented safety
procedures.

## Engineering contribution

The repository demonstrates more than a bootloader implementation. It combines:

- secure-boot architecture;
- cryptographic image tooling;
- redundant metadata and slot policy;
- authenticated update preparation;
- deterministic builds;
- host-side verification;
- hardware-in-the-loop automation;
- backup and restoration verification;
- traceable experiment documentation.

The result is a research platform that can be extended without losing the
ability to compare later experiments against a validated baseline.
