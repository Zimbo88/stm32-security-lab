# EXP032 – Device and Board Capability Inventory

## Objective

Establish a reproducible baseline inventory of the STM32F429IGT6 target,
development board, debug connection and host toolchain.

## Planned observations

- ARM core identity
- CPUID
- DBGMCU identity code
- flash size register
- unique device identifier
- option-byte register state
- reset and clock register state
- SCB and NVIC configuration
- debug-port availability
- linker memory layout
- installed host tools
- connected USB debug and serial devices
- visible board components
- available headers and test points

## Safety

This experiment is read-only.

No option bytes are modified.

RDP is not changed.

Flash protection is not changed.

## Expected result

A complete baseline inventory suitable for all later STM32F429 security and
architecture experiments.
