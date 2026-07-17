# Current State Audit

Audit date: 2026-07-17. This audit is based on repository inspection and
offline builds only. No board was flashed, erased, reset, or reconfigured.

Post-audit hardening note: subsequent commits corrected the EXP066 vector
table, introduced the generated STM32F429 memory layout, tightened canonical
manifest validation, hardened the signer, added host C verifier tests, and
added CI/reproducibility checks. A later Stage-0 hardening pass added
redundant pre-jump validation, compile-time layout assertions, final flash
vector re-read before handoff, and expanded malformed-image boundary tests.
Historical observations below are preserved as audit evidence; use
`firmware/exp045_bootloader_v2/README.md`, `docs/memory_layout.md`,
`docs/secure_boot_validation.md`, and the current source for the implemented
baseline.

## Repository overview

The repository contains a sequence of bare-metal STM32F429 experiments,
captured measurements and recovery images, host scripts, and a vendored copy of
Monocypher 4.0.3. The current secure-boot reference is
`firmware/exp045_bootloader_v2`; the current signed application is
`firmware/exp065_signed_app`.

The working tree was not clean when this audit began. In particular,
`board_clock.c` and `firmware_public_key.h` in EXP045 were modified, and EXP065,
the board image, schematic, and a pre-EXP065 public-key header were untracked.
These files are treated as user-owned work and were not reverted.

No repository-wide CMSIS device headers, STM32 HAL, LL, or libopencm3 are used
by the reference projects. They use direct volatile register access and local
register definitions. The compiler target is ARM Cortex-M4 Thumb.

## Verified build commands

With `arm-none-eabi-gcc 13.2.1`, GNU binutils 2.42, Python 3.12.3, and PyNaCl
1.5.0:

```sh
make -C firmware/exp045_bootloader_v2 clean
make -C firmware/exp045_bootloader_v2 all report
make -C firmware/exp065_signed_app clean
make -C firmware/exp065_signed_app all
make -C firmware/exp065_signed_app signed
make -C firmware/exp065_signed_app report
```

The clean builds succeeded with `-Wall -Wextra -Werror`. EXP045 also enables
stack-usage output. Baseline artifacts from the initial clean build were:

| Artifact | Bytes | SHA-256 |
|---|---:|---|
| EXP045 bootloader binary | 13,808 | `21061f8ccabe50da1db973ec0efc091fc223125ee72073247b30e60666e226ce` |
| EXP065 application payload | 456 | `6d40897f54674f3b2aef96e8f56dc69513ca0157ecebb5f74c666eee59e58511` |
| EXP065 signed image | 968 | `9bb74b5ae3d35b84b1f59f45be1de50ccfc8fd1b4aab5d00c344662225b908aa` |

These hashes describe the initial dirty working-tree baseline from the audit
start, not the post-EXP067 hardening commit.

EXP066 was completed as a separately linked Stage-1 research platform during
this audit. Its clean build succeeds with the same toolchain and produces an
EXP065-compatible signed image. It uses the existing `0x08008200` vector
address and does not alter the Stage-0 linker layout. The current platform
provides startup, UART command dispatch, device identity, fixed diagnostic
snapshots, a bounded RAM log, retained fault record, non-destructive test
placeholders, and a module-manager placeholder.

EXP067 was completed as a host-side module package milestone. The package
parser is pure and fuzzable, verifies the CRC/hash/signature layout, enforces
bounded capability tables, and does not execute package bytes. The redundant
catalog helper validates fixed-size CRC-protected records and can select the
newest valid record after corrupted records.

EXP068 through EXP070 added host-side bytecode execution, constrained native
module validation/simulation, and atomic A/B module-installation simulation.
These are not target-firmware module execution or target Flash installation
paths.

EXP071 completed the pre-hardware integration pass for EXP066. The target
platform now has a non-blocking LED health service, active-low board LED GPIO
adapter for PE3/PH10/PH11/PH12, reset/fault-to-health policy, boot status,
health status/acknowledge commands, bounded LED tests, and a bounded
`easteregg knightrider` command. Optional audio remains unavailable because no
speaker/buzzer pin is documented. The feature matrix in
`docs/platform-feature-matrix.md` is authoritative for target, host,
simulation, design-only, and not-implemented boundaries.

## Verified memory layout

| Region | Address range | Reference implementation |
|---|---|---|
| Stage 0 bootloader | `0x08000000`-`0x08007fff` | 32 KiB linker region |
| Signed manifest | `0x08008000`-`0x0800805f` | 96 bytes |
| Ed25519 signature | `0x08008060`-`0x0800809f` | 64 bytes |
| Reserved padding | `0x080080a0`-`0x080081ff` | filled with `0xff` by signer |
| Application vector/payload | `0x08008200`-`0x080fffff` | linker assumes first 1 MiB Flash bank only |
| Main SRAM used by linker | `0x20000000`-`0x2001ffff` | 128 KiB |

The board header describes a 2 MiB Flash device, 256 KiB SRAM, and 64 KiB CCM,
but the reference linkers intentionally use only 1 MiB Flash and the first 128
KiB SRAM. The built EXP065 vector table is at `0x08008200`, its initial MSP is
`0x20020000`, and its Thumb reset vector is `0x08008241`.

## Trust and signing model

Stage 0 embeds one 32-byte Ed25519 public key. The development signer hashes
the exact application payload with SHA-512, puts that digest and fixed metadata
in a 96-byte little-endian manifest, and signs exactly the manifest with
Ed25519. The bootloader validates fixed format/version fields, a compiled image
version floor of 2, vector address, image size, MSP, Thumb reset vector, payload
SHA-512, and manifest signature. It fails closed on any error.

The application signing seed is stored in
`firmware/exp065_signed_app/keys/firmware_signing_seed.bin`, mode 0600, and is
ignored by EXP065's local `.gitignore`. The repository also contains historical
laboratory signing material. Notably, `signing/exp016/firmware_signing_private.pem`
is present in the working tree even though it is not currently tracked. Such
keys must be considered development-only and must never be production anchors.

The Python dependencies are not yet recorded in a requirements or lock file.
PyNaCl is imported directly by the signing scripts.

## Current boot flow

Reset initializes `.data` and `.bss`, retains reset-default HSI16, initializes
USART1 on PA9/PA10 at 115200 baud, records and clears RCC reset flags, evaluates
a recovery stub, and verifies the signed image. Recovery is deliberately
unavailable. On success, Stage 0 disables interrupts and SysTick, disables and
clears eight NVIC banks, changes VTOR, installs the application MSP, reenables
interrupts, and calls the application reset handler.

## Current limitations

- The manifest has fixed fields but reserved fields and flags are not checked
  for zero.
- The reset-vector end calculation is not explicitly overflow checked, although
  the fixed Flash bound currently constrains image size.
- The MSP upper bound is accepted inclusively and models only the first 128 KiB.
- Rollback policy is a compiled constant, not monotonic persistent state.
- Recovery and authenticated update are placeholders only.
- Target-side module execution and target-side atomic module installation are
  not implemented; EXP068-EXP070 remain host tools/simulations.
- EXP071 LED health indication is built into EXP066 but still needs hardware
  validation for polarity, timing, and UART responsiveness.

## Build and repository caveats

The initial Git status contains unrelated user work (modified EXP045 clock and
public-key files, an untracked EXP065 project, board assets, and an earlier
EXP066 scaffold). Milestone-relevant source was later committed; no files were
reverted and no hardware operations were performed.
Generated `build/` outputs are local verification artifacts and are not being
flashed or committed as part of this audit.
- The reference vector tables contain only the 16 core entries; peripheral IRQs
  are absent.
- Stage 0 fault handling is a spin-only default handler.
- UART assumes the reset-default 16 MHz clock.
- No host unit tests exercise the embedded verifier.
- EXP065 source is tracked, but the private signing seed remains intentionally
  ignored and must be supplied out-of-band for signed-image reproduction.

## Known hardware assumptions

The target is an STM32F429IGT6 Cortex-M4 board. Repository evidence identifies
a 25 MHz HSE, 32.768 kHz LSE, active-low LEDs on PE3/PH10/PH11/PH12, USART1 on
PA9/PA10, a wake/user input on PA0, external SDRAM, and other board-specific
connections. EXP045/EXP065 use only HSI16 and internal memory. Hardware behavior
has not been revalidated during this audit.

## RDP2 and board-bricking risks

- RDP Level 2 is irreversible and removes normal debug/recovery paths.
- The present recovery path is not implemented; a bad bootloader or trust
  anchor can therefore make signed application recovery impossible.
- A lost or compromised signing key can make future authenticated maintenance
  impossible or untrustworthy.
- Bootloader write protection must cover every sector occupied by the entire
  32 KiB Stage 0, and the final option-byte policy must be independently checked.
- Clock, vector-table, Flash geometry, SRAM geometry, and power-loss behavior
  must be tested on the exact board revision before irreversible provisioning.
- Stage 1/module parsers, update journals, watchdog recovery, and persistent
  fault-loop handling do not yet have hardware power-interruption evidence.
- External SDRAM cannot be a trust anchor or sole persistent state store.

## Files that must remain stable

Until a separately reviewed migration is complete, preserve the EXP045 linker
layout, `signed_image.[ch]`, `flash_layout.h`, `image_policy.h`, embedded public
key, jump logic, startup, UART/clock assumptions, and the EXP065 linker/signing
format. Preserve the private signing seed outside committed content and maintain
verified recovery copies of the exact bootloader, signer, public key, and signed
platform image.

## Recommendations before any future RDP2 provisioning

Do not provision RDP2 yet. First freeze and independently review Stage 0; add an
authenticated, physically selected recovery flow; validate all option bytes and
write-protection coverage; establish offline key backup and rotation policy;
complete parser fuzzing and update power-loss tests; perform repeated cold-boot,
brownout, watchdog, invalid-image, and recovery tests on expendable hardware;
archive reproducible artifacts and hashes; and require a separate written,
two-person provisioning checklist with an explicit final confirmation.
