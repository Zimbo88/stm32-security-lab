# EXP071 platform integration, health indication, diagnostics, and release readiness

EXP071 turns the signed EXP066 research platform into a coherent pre-hardware
integration release. It does not flash hardware, modify option bytes, activate
RDP, or claim that host simulations are hardware-complete.

## Implementation boundaries

- Implemented in target firmware: EXP066 UART CLI, curated read-only
  diagnostics, RAM log, retained fault record display/clear, reset-cause
  capture, non-blocking LED health service, LED test commands, and disabled
  audio boundary.
- Implemented as host tooling: EXP067 signed packages, EXP068 bytecode VM,
  EXP069 native module validator/simulator, and EXP070 module install
  simulator.
- Simulated only: bytecode/native module execution, native-module lifecycle,
  atomic A/B module installation, power-loss recovery, rollback, and
  quarantine behavior.
- Designed but not hardware-validated: MPU isolation for native modules,
  hardware update flow, watchdog recovery, brownout behavior, and module slots
  in real Flash.
- Not implemented: target-side module execution, target-side module install,
  USB/Ethernet/LCD/SDRAM/gyroscope drivers, arbitrary memory/register access,
  Flash write commands, option-byte write commands, and RDP commands.

## Health-state model

EXP066 now has a central `platform_health` service driven by the bounded
`platform_idle()` periodic service call. It does not use delay loops for normal
runtime LED animation. The health service writes logical LED masks to a
target-specific GPIO adapter in `platform_led.c`.

Priority is enforced so lower-priority states cannot accidentally overwrite
critical states.

| Priority | State | Default use |
|---:|---|---|
| 8 | `SECURITY_FAILURE` | Current signature/security failure indication. |
| 7 | `FAULT` | Current fatal platform/self-test fault. |
| 6 | `RECOVERY` | Recovery/update recovery indication. |
| 5 | `UPDATING` | Candidate/update progress indication. |
| 4 | `DEGRADED` | Historic watchdog or retained fault record after stable boot. |
| 3 | `TEST_RUNNING` | Bounded non-destructive test indication. |
| 2 | `BOOTING` | Early platform initialization. |
| 1 | `HEALTHY` | Normal idle state after boot/self-tests pass. |

## LED pattern table

The board LED mapping is centralized in `platform_led.h`: PE3, PH10, PH11, and
PH12, active-low. Higher-level code uses only logical LED masks.

| State | Pattern |
|---|---|
| `BOOTING` | Slow progressive fill, then off. |
| `HEALTHY` | Continuous Knight-Rider scan: LED1, LED2, LED3, LED4, LED3, LED2. |
| `DEGRADED` | Two short all-LED flashes followed by a pause. |
| `UPDATING` | Directional progress fill and drain. |
| `RECOVERY` | Alternating outer LEDs and inner LEDs. |
| `TEST_RUNNING` | Short all-LED pulse followed by off time. |
| `FAULT` | Rapid repeated all-LED fault burst. |
| `SECURITY_FAILURE` | Critical all/outer/inner sequence distinct from `FAULT`. |

Bounded LED tests and Easter eggs restore the automatic health state when their
duration expires or when stopped.

## Reset and fault behavior

On boot, EXP066 captures `RCC_CSR`, checks the retained `.noinit` fault record,
logs both conditions, runs mandatory self-tests, and applies this policy:

- current self-test failure: `FAULT`;
- current signature/security failure API call: `SECURITY_FAILURE`;
- previous watchdog reset or valid retained HardFault record with current boot
  otherwise stable: `DEGRADED` until `health acknowledge`, `fault clear`, or the
  next reset policy evaluation;
- current boot with no retained fault/watchdog evidence and self-tests passing:
  `HEALTHY`.

HardFault context still captures only bounded CPU/fault state in `.noinit` RAM
and then stops. It does not perform Flash writes.

`fault clear` invalidates retained fault evidence and is therefore a restricted
diagnostic command in the Runtime Security Monitor policy.

## CLI commands

The EXP066 prompt is:

```text
rp>
```

New allowlisted commands:

```text
health status
health acknowledge
led status
led test healthy
led test degraded
led test update
led test recovery
led test fault
led test security
led test stop
boot status
easteregg knightrider
easteregg retro
easteregg stop
```

LED test commands are bounded and non-blocking. They do not write Flash, modify
option bytes, alter RDP, change clocks, or expose arbitrary GPIO control.

## How to trigger the Easter egg

At the `rp> ` prompt:

```text
easteregg knightrider
```

Stop it early with:

```text
easteregg stop
```

The Knight-Rider Easter egg is LED-only, bounded, and returns to automatic
health indication.

## Optional audio status

Audio is not implemented by default. The repository does not document a
speaker, buzzer, or timer-capable audio pin for this board. The command:

```text
easteregg retro
```

returns an explicit unavailable message. Real audio support requires an
external passive piezo/buzzer, an assigned timer-capable GPIO, schematic update,
and separate hardware validation.

## Remaining hardware-validation tasks

- Verify LED polarity and pin mapping on the exact board revision.
- Verify UART responsiveness while LED service runs.
- Validate reset-cause capture after real watchdog, pin reset, POR/BOR, and
  software reset.
- Validate retained fault record behavior after an induced fault on expendable
  hardware.
- Validate signed EXP066 launch from Stage 0 and confirm the `VTOR` self-test.
- Validate power-loss behavior for any future target-side update manager.
- Validate watchdog servicing once watchdog support is enabled.

## RDP2 status

RDP2 must still not be enabled. Recovery remains incomplete, target-side update
installation is not hardware-validated, power-loss behavior is host-simulated
only, and there is no independently reviewed provisioning checklist for the
exact board. RDP2 would remove normal debug/recovery paths before the platform
has proven hardware recovery behavior.
