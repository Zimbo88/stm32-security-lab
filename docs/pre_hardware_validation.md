# Pre-Hardware Validation Readiness

This phase is software-only preparation for controlled STM32F429 hardware
validation. It does not flash a board, alter option bytes, enable WRP/RDP, or
perform voltage, clock, reset, electromagnetic, laser, or other physical fault
injection.

## Diagnostic Security Boundary

Memory-integrity tooling and runtime telemetry are diagnostic evidence only.
They can report that observed bytes or registers match a reference, differ from
a reference, or look consistent with a simple modification pattern. They never
make an image trusted. Bootability remains decided only by the existing
Ed25519/SHA-512 signed-image verifier, vector-table policy, metadata recovery,
trial-boot state machine, and confirmation policy.

The production-compatible flash layout has no spare persistent diagnostic
sector. Runtime experiment reports therefore live in RAM and are emitted through
the existing diagnostic output. Canary provisioning is host-side and requires an
explicit laboratory region allowlist; normal firmware does not write canaries.

## Integrity Reference Workflow

Create a reference manifest from a complete 2 MiB flash image:

```sh
python3 tools/memory_integrity.py create-reference \
  --flash-dump flash-reference.bin \
  --build-identity exp066-slot-a-prehw \
  --expected-slot a \
  --option-snapshot FLASH_OPTCR=0x0FFFAAED \
  --boot-register-snapshot RCC_CSR=0x0C000000 \
  --json-output reference.json \
  --report
```

Compare a later dump:

```sh
python3 tools/memory_integrity.py compare \
  --reference reference.json \
  --flash-dump flash-after.bin \
  --reference-dump flash-reference.bin \
  --option-snapshot FLASH_OPTCR=0x0FFFAAED \
  --boot-register-snapshot RCC_CSR=0x0C000000 \
  --json-output comparison.json \
  --report
```

The reference schema records the schema/tool version, Git commit, build
identity, target identifier, memory-layout digest, expected slot, region ranges
and purposes, region/sector/block SHA-512 values, CRC32 values, signed-image
manifest details for both slots when present, supplied option-byte snapshots,
supplied boot-state register snapshots, and deterministic generation metadata.

Direct observations are byte equality, hashes, CRC32, bit-transition counts,
first/last differing address, and bounded example differences. Classifications
such as erased, partially erased-looking, copied block, or shifted block are
heuristics and are labelled as such.

## Canary Patterns

The host tool can generate deterministic canary bytes for an explicitly
allowlisted laboratory region:

```sh
python3 tools/memory_integrity.py canary-provision \
  --region lab:0x08100200:0x08100600:address \
  --allow-lab-region lab:0x08100200:0x08100600 \
  --active-slot a \
  --output canary.bin
```

Supported patterns are `all00`, `allff`, `aa55`, `address`, `prng`,
`walking_one`, and `walking_zero`. The default laboratory preference is
address-derived or deterministic pseudo-random data, because the expected byte
for any address can be recomputed without storing a second flash copy.

Provisioning is rejected for Stage 0, boot metadata, update metadata, the active
slot, recovery, option bytes, OTP, system memory, and any address outside
internal flash.

## Runtime Telemetry

EXP066 maintains a bounded RAM report with magic `STMR`, format version, boot
counter, reset-cause snapshot, selected/confirmed/candidate slots, metadata
state and sequence, remaining trial attempts, image version, VTOR, MSP, PSP,
CONTROL, PRIMASK, BASEPRI, FAULTMASK, integrity-scan status, first changed
region, last boot-policy result, last update/confirmation result, trusted-state
flags, untrusted-observation flags, commit marker, and CRC32.

`telemetry show` prints the current RAM report. A binary report with the same
field order is decoded offline with:

```sh
python3 tools/memory_integrity.py decode-telemetry \
  --input telemetry.bin \
  --json-output telemetry.json
```

Reset causes are captured from `RCC_CSR` before reset flags are cleared.
`reset decoded` names power-on, pin, software, independent watchdog, window
watchdog, brownout-related, and low-power reset flags. A reset flag is only an
observation; it does not prove a specific attack.

RAM diagnostics are non-destructive in normal builds: VTOR consistency and
MSP/PSP SRAM range are recorded as observations. Destructive RAM tests are
reserved for explicit laboratory-only diagnostics with dedicated linker-reserved
memory.

## Target Flash Backend

The STM32F429 target backend is wired only through `boot_flash`. Normal target
initialization remains read-only or fail-closed. Writable initialization is
limited to:

- redundant metadata through `boot_flash_target_init_metadata`
- inactive-slot installation through the authenticated installer and its
  existing write-region policy
- explicitly declared laboratory canary regions only when
  `BOOT_ENABLE_LAB_CANARY_WRITES` is enabled

The backend unlocks the flash controller, clears status flags, uses bounded busy
polling, erases complete generated-layout sectors, programs bytes using the
conservative voltage-compatible programming width, checks error flags, relies on
the flash abstraction for read-back verification, and re-locks on every exit.
Zero-length programming is a no-op before backend entry.

STM32F429 flash operations cannot safely call flash-resident helper code while
the controller is busy. The erase/program critical routines and busy wait are
placed in `.ramfunc`, copied by startup before `.data`, and checked before
writable target initialization succeeds. Interrupts are disabled only around the
critical flash-controller operation and the saved PRIMASK state is restored on
every exit. The data cache is disabled and reset around flash writes per the
documented STM32F429 read-while-write cache limitation.

Voltage-dependent programming assumptions still require controlled hardware
validation before destructive flash testing.

## Application Confirmation Health Gate

EXP066 does not confirm immediately at reset. After the first stable idle point
or an explicit laboratory boot-delay gate, it attempts confirmation once only
when all conditions are true:

- early platform initialization completed
- the running slot was identified from VTOR
- mandatory core self-checks passed
- no critical initialization failure was recorded
- a stable execution point was reached
- metadata shows the running image is the pending candidate, or already the
  confirmed image for idempotence

The service calls the existing `boot_confirm_current_slot` API, so confirmation
is restricted to the currently booted pending slot and remains power-loss-safe.
Ambiguous metadata, invalid metadata, wrong slot, and metadata commit failures
fail closed and are reported.

## Slot A And Slot B Releases

Build and verify both application slots without hardware:

```sh
make -C firmware/exp066_research_platform_core slot-releases \
  SIGNING_SEED=/path/to/development_or_release_seed.bin \
  PUBLIC_KEY_HEADER=../exp045_bootloader_v2/src/firmware_public_key.h
```

The target produces Slot A and Slot B ELF, BIN, HEX, map, authenticated update
package, package inspection JSON, and offline verification JSON under
`build/slot_a` and `build/slot_b`. The build verifies vector address, linker
origin, bounds, reset handler, manifest slot binding, package target
compatibility, and package signature. `tools/check_deterministic_build.py`
compares both slot release outputs across exported clean checkouts.

## Pre-Hardware Checklist

- `python3 tools/emit_memory_layout.py --check`
- host verifier tests, update-storage tests, sanitizer variants, and Python
  tests pass
- `make -C firmware/exp045_bootloader_v2 clean all report` reports Stage 0
  within its reserved region
- EXP066 Slot A and Slot B releases build and verify offline
- deterministic-build comparison passes
- private-key scan passes
- `git diff --check` passes for task-owned paths
- no production private key, flash dump, temporary seed, or build artifact is
  staged
- option bytes, WRP, RDP, and hardware rollback counters remain unchanged

## Deferred Work

Hardware validation still needs controlled board flashing, serial observation,
trial boot, confirmation, fallback, flash erase/program timing, voltage
assumption checks, reset-cause observation, and power-loss behavior tests.
WRP/RDP, option-byte programming, irreversible provisioning, hardware-backed
rollback protection, UART/USB/network update transport, and physical
fault-injection campaigns remain explicitly deferred.
