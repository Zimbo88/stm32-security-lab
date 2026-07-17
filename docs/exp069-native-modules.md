# EXP069 native signed modules

EXP069 adds validation and lifecycle handling for trusted, signed native ARM
Thumb research modules. It uses the existing EXP067 signed package envelope and
accepts only `TYPE_NATIVE` packages. It does not implement an ELF loader, a
dynamic linker, unrestricted symbol resolution, writable-executable memory, or
host-native execution.

## Native payload format

The package payload starts with a fixed `NMV1` header followed by contiguous
code, rodata, and initial data regions. BSS and stack are reservations, not
payload bytes. All integer fields are little-endian.

Fields validated by `tools/native_loader.py` include:

- `magic`, format version, header size, total size, and reserved fields,
- code, rodata, data, bss, and stack sizes,
- code, rodata, and data offsets,
- entry offset,
- platform API version,
- simulation ID used by the host-side validator.

The loader requires aligned, ordered, non-overlapping regions. The code region
must begin immediately after the `NMV1` header, inter-region padding must be
zero-filled, and trailing bytes after initial data are rejected. Code size must
be nonzero and halfword-aligned. The entry offset must be halfword-aligned and
inside the code region. Resource limits are intentionally small:

- code: 4096 bytes,
- rodata: 2048 bytes,
- data: 1024 bytes,
- bss: 1024 bytes,
- stack: 1024 bytes,
- total module RAM: 1024 bytes.

Native code is treated as opaque ARM Thumb bytes by the host validator. The
example module uses a harmless Thumb stub plus host-side simulation metadata.

## Platform API table

The platform API table is stable and versioned. EXP069 defines API version 1:

| Service | Required capability | Behavior |
|---:|---:|---|
| `1` / `read_rcc` | `0x00006801` | Return a curated RCC snapshot value by fixed index. |
| `2` / `emit` | `0x00006802` | Emit a bounded `(channel, value)` result. |

Modules do not receive arbitrary addresses. There is no Flash write service, no
option-byte service, no RDP service, no arbitrary memory read/write service, and
no hidden diagnostic interface.

## Capability enforcement

The signed package capability table is authoritative. The native loader rejects
unknown capabilities before loading. Each privileged API service checks its
required capability. Native execution remains disabled unless package
verification, compatibility checks, resource validation, API validation, and
capability checks all pass.

## Lifecycle and quarantine

The host-side lifecycle is:

1. verify signed package, signer, module ID, ABI, platform, rollback floor, and
   capabilities;
2. load code/rodata/data/bss/stack into bounded host-side structures;
3. initialize;
4. run through the simulator;
5. stop;
6. quarantine after repeated failures.

The failure threshold is configurable. Repeated initialization or run failures
move the module to `QUARANTINED` and prevent normal execution. Quarantine is a
terminal state for the loaded module: later initialize or run attempts are
rejected unless a new module is explicitly verified and loaded.

## Example module

`modules/exp069_native_rcc_module.md` documents the first harmless native module.
It analyzes a supplied RCC snapshot and emits raw `RCC_CR`, `RCC_CR.HSIRDY`,
`RCC_CR.PLLON`, `RCC_CFGR.SWS`, and raw `RCC_CSR` through the approved output
service.

## Threat model and limitations

EXP069 fails closed for invalid signatures, wrong signer identity, wrong ABI,
unsupported platform version, rollback, malformed offsets, integer overflow,
unaligned entry points, entries outside code, forbidden capabilities, excessive
resources, malformed API tables, lifecycle failures, and quarantine thresholds.

Complete MPU isolation is a firmware design requirement, but it is not
demonstrated in EXP069 because no hardware execution is performed. The host
simulator validates format, lifecycle, and policy behavior without executing ARM
or host-native code.
