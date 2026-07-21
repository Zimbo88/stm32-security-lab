# Runtime Security Monitor

EXP066 contains a phase-1 Runtime Security Monitor (RSM) foundation for the
Authenticated Runtime Flight Recorder research path. The monitor is a
diagnostic and forensic component. It is not a separate security enclave and it
does not claim to detect an attacker with full runtime code execution.

## Data Flow

```text
verified boot -> runtime ready -> periodic checks -> anomaly -> fault/reset -> next-boot import
```

Stage-0 remains the trusted computing base. It validates the signed image
manifest, SHA-512 payload hash, Ed25519 signature, slot policy, vector table,
initial MSP, and reset handler before jumping to the application. Phase 1 does
not add a boot mailbox, so the runtime status reports this as
`verified_launch_assumed_no_mailbox` evidence rather than external
attestation.

The runtime monitor reuses existing EXP066 state:

- `log.c` for the RAM event ring and dropped counter.
- `platform_health.c` for the health state.
- `experiment_telemetry.c` for the `.noinit` boot counter and metadata
  observation.
- `fault.c` for retained fault-record validity.
- `platform_confirmation.c` and boot metadata APIs for slot/version context.

No Flash layout, option bytes, recovery sector, or Stage-0 code is changed by
the phase-1 monitor.

## Information Classes

- `PUBLIC`: chip family, device/revision ID, flash size, UID fingerprint,
  active slot, firmware version when available, reset cause, clock summary,
  health state, pass/fail/unavailable checks, and aggregate counters.
- `RESTRICTED`: full UID, exact option bytes, exact addresses, raw register
  snapshots, detailed telemetry, detailed event history, and fault PC/LR.
  This is disabled by default and requires `RSM_RESTRICTED_DIAGNOSTICS=1`.
- `SECRET`: keys, authentication secrets, update secrets, full RAM/Flash dumps,
  and protected payload data. EXP066 never emits this class.

The UID fingerprint is CRC32 over the twelve UID bytes formed by serializing
`UID0`, `UID1`, and `UID2` as little-endian 32-bit words. It is not a
cryptographic hash and must not be used for authentication; collisions are
possible.

## Key-Value Schema

`rsm status` and `rsm status public` emit stable, newline-delimited keys. The
values below are illustrative examples, not hardware measurements from a
specific board:

```text
rsm.schema=1
rsm.status=ok
rsm.build.diagnostic_mode=production_default
rsm.device.family=STM32F429
rsm.device.id=0x00000419
rsm.device.revision=0x00001003
rsm.device.flash_kib=1024
rsm.device.uid_fingerprint=8A42D119
rsm.cpu.core=Cortex-M4F
rsm.cpu.fpu=enabled
rsm.cpu.mpu=disabled
rsm.cpu.clock_source=hsi
rsm.cpu.hclk_hz=16000000
rsm.cpu.cpuid=0x410FC241
rsm.boot.sequence=53
rsm.boot.slot=A
rsm.boot.firmware_version=7
rsm.boot.last_reset=iwdg
rsm.security.flash=unavailable
rsm.security.vectors=pass
rsm.security.stack=unavailable
rsm.security.option_policy=unavailable
rsm.health.state=healthy
rsm.evidence.last_event_sequence=91
rsm.evidence.last_fault=none
rsm.evidence.timebase=relative
rsm.evidence.boot_context=verified_launch_assumed_no_mailbox
rsm.events.count=8
rsm.events.dropped=0
rsm.counter.diagnostic_denials=0
rsm.counter.event_log_overflows=0
rsm.policy.restricted=disabled
rsm.policy.secret=never
```

Unavailable values are printed as `unavailable`; no JSON library or dynamic
allocation is used.

`RSM_RESTRICTED_DIAGNOSTICS=1` marks the output with
`rsm.build.diagnostic_mode=restricted_development`. It is a development build
switch only and does not add authentication. `RSM_ENABLE=0` keeps the CLI
command present but reports the monitor as disabled.

## UART Policy

The confirmed EXP066 diagnostic UART is USART1 on PA9/PA10, AF7, 115200 8N1,
3.3 V TTL. No second diagnostic UART is configured in phase 1 because no
second independent pin mapping has been validated in the repository.

## Limits

Phase 1 does not implement a vector-table monitor, stack canary, incremental
Flash scan, option-byte policy baseline, second UART owner arbitration, or
persistent flight-recorder Flash area. Those require later phase approval.

RDP Level 2 is never enabled, changed, or automated by this monitor. Option
bytes are only read when restricted diagnostics are explicitly enabled.
