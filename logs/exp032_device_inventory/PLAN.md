# EXP032 – Device and Board Capability Inventory

## Objective

Establish a reproducible baseline inventory of the STM32F429IGT6 target,
development board, SWD connection and host toolchain.

## Planned observations

- ARM core identity
- SCB CPUID register
- DBGMCU device and revision identity
- internal flash size
- 96-bit unique device identifier
- flash option-control register state
- reset-cause register state
- clock-control register state
- vector-table location
- fault-status registers
- SWD availability
- linker memory layout
- installed host tools
- connected USB debug and serial devices
- visible board components
- available headers and test points

## Safety

This experiment is read-only.

No flash memory is programmed or erased.

No option bytes are modified.

RDP is not changed.

WRP and PCROP are not changed.

## Expected result

A complete baseline inventory suitable for later STM32F429 architecture,
peripheral, boot and security experiments.
