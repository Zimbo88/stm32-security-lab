# Phase 2A vector monitor hardware test

This checklist validates the Phase 2A Runtime Security Monitor vector-table
monitor on the STM32F429IGT6 board. It uses only existing firmware and CLI
commands; it does not require artificial vector manipulation or new test hooks.

## Firmware

Build and flash the default EXP066 application image:

- Stage-0 bootloader, if the board does not already contain the reviewed
  bootloader: `firmware/exp045_bootloader_v2/build/exp045_bootloader_v2.bin`.
- EXP066 default build payload:
  `firmware/exp066_research_platform_core/build/exp066_research_platform_core.bin`.

Use the repository's existing signed-image and slot-provisioning flow before
booting through Stage-0. Do not flash an unsigned payload as a production-like
secure-boot result.

## UART

Use the existing EXP066 diagnostic UART:

- USART1
- PA9 TX, PA10 RX
- AF7
- 115200 8N1
- 3.3 V TTL levels

The prompt is `rp> `.

## CLI commands

Run these commands after reset:

```text
version
boot status
health status
led status
rsm status
rsm status restricted
log show
```

The default build should deny `rsm status restricted` with the restricted
diagnostic policy. Do not build with `RSM_RESTRICTED_DIAGNOSTICS=1` for this
public hardware smoke test unless the board is explicitly treated as a
development diagnostic target.

## Expected public RSM output

`rsm status` or `rsm status public` must contain stable key-value lines:

```text
rsm.security.vectors=pass
rsm.vector.status=pass
rsm.vector.failure_class=none
rsm.vector.failures=0
rsm.vector.latched_failure=no
rsm.security.flash=unavailable
rsm.security.stack=unavailable
rsm.security.option_policy=unavailable
rsm.evidence.boot_context=verified_launch_assumed_no_mailbox
rsm.policy.restricted=disabled
rsm.policy.secret=never
```

`rsm.vector.checks` may be `0` immediately after reset and should increase
after the main loop has completed one or more bounded vector-check cycles.

## Health and LEDs

For a normal boot with no retained watchdog or fault evidence, `health status`
should report `HEALTHY`, and the LEDs should show the documented HEALTHY scan
over PE3, PH10, PH11, and PH12. If previous reset or retained fault evidence is
present, `DEGRADED` is acceptable until it is acknowledged or cleared through
the existing documented commands. `SECURITY_FAILURE` is not expected in the
normal Phase 2A smoke test.

## Intentionally hidden information

The default public output must not expose:

- full UID
- exact vector-table entries
- expected or observed VTOR values
- fault PC or LR
- MSP or PSP values
- raw option bytes
- arbitrary memory or register dumps

Exact vector details are restricted diagnostics and are unavailable in the
production-default build.

## Pass criteria

Phase 2A is functioning on hardware when:

- Stage-0 launches the reviewed EXP066 image through the existing flow.
- The CLI remains responsive on USART1.
- `rsm.security.vectors=pass` and `rsm.vector.status=pass` are present.
- `rsm.vector.failure_class=none` and `rsm.vector.latched_failure=no` are
  present.
- `rsm.vector.checks` increases after runtime activity.
- Health remains `HEALTHY` or a documented pre-existing `DEGRADED` state.
- Restricted vector addresses and SECRET-class data do not appear in public
  output.

## Known limits

- Not yet hardware-validated.
- No external attestation.
- No boot mailbox.
- No flash-integrity scan.
- No persistent evidence chain.
- No automatic recovery, reset, or slot switch on vector findings.
