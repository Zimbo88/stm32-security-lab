# Project Scope

STM32 Security Lab is a controlled, open-source research platform for
reviewing secure boot, authenticated A/B firmware updates, recovery policy,
runtime diagnostics and reproducible evidence on STM32F429 hardware.

## Intended audience

- embedded developers studying secure update paths;
- security researchers and reviewers;
- students and educators;
- maintainers of the reference implementation;
- researchers with the documented STM32F429 test hardware.

## Explicit boundaries

This repository is not a commercial product, certified security module,
general STM32 programming library, or plug-and-play firmware for arbitrary
boards. It does not guarantee resistance to invasive attacks, fault
injection, side channels or undocumented board variations. No release is a
production approval. Independent review and board-specific validation remain
required.

The normal software path is host-testable and does not require hardware. The
hardware path is deliberately limited to the documented STM32F429IGT6-class
layout. RDP2, Write Protection and other irreversible protection settings are
not enabled by repository automation.

## Evidence vocabulary

The repository uses these labels literally:

- `IMPLEMENTED`: present in the current source tree;
- `HOST TESTED`: exercised by a deterministic host test;
- `HARDWARE VALIDATED`: observed on the documented target and recorded in a
  report;
- `LIMITED VALIDATION`: only a subset of the behavior was observed;
- `DOCUMENTED ONLY`: described but not implemented or executed;
- `NOT IMPLEMENTED`: explicitly outside the current scope.

See [Security Claims](security-claims.md) and the feature matrix for the
evidence supporting individual statements.
