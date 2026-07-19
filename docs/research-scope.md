# Research Scope

## Purpose

This repository supports defensive research into STM32 firmware trust,
boot-chain behavior, update handling, protection mechanisms, and observable
device state.

## Included work

- authenticated boot;
- image and manifest verification;
- redundant metadata;
- A/B slot policy;
- rollback control;
- update preparation and installation;
- recovery behavior;
- host-side image inspection;
- hardware-in-the-loop validation;
- device and debug-state observation;
- documented negative testing.

## Planned or adjacent work

- controlled fault injection;
- voltage and clock glitching;
- analysis of altered protection settings;
- boot-time electrical observation;
- side-loading and malformed loading attempts;
- comparison of memory state before and after security experiments;
- transfer of methods to closely related STM32F429 variants.

## Authorization boundary

All invasive tests must be performed on hardware owned by or explicitly
entrusted to the researcher. This repository does not authorize testing of
third-party devices or bypassing protections without permission.

## Interpretation of results

A test result should identify whether the observed outcome originated from:

- secure-boot policy;
- application behavior;
- target reset state;
- flash transport;
- debugger transport;
- serial capture;
- host tooling;
- hardware fault injection;
- an unclassified condition.

This separation is necessary for defensible research conclusions.
