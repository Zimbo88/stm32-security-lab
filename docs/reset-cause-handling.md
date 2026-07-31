# Reset-cause handling

Status: IMPLEMENTED and host tested for normalization. Hardware evidence is
not claimed here.

Stage-0 captures `RCC_CSR` before clearing anything. Current Stage-0 no longer
clears the flags before the application; the application captures the same
snapshot into `.noinit` telemetry and then clears `RMVF` after recording it.

The normalized priority for combined flags is:

1. `IWDG`
2. `WWDG`
3. `SOFTWARE`
4. `BROWNOUT`
5. `PIN`
6. `POWER_ON`
7. `LOW_POWER`
8. `NONE` if no reset flag is set
9. `UNKNOWN` only for an unrecognized flag combination

This priority preserves the most actionable failure when a watchdog and a
software/pin flag coexist. Stage-0 emits one machine-readable line and keeps
the existing human-readable flag lines:

```text
BOOT_RESET cause=IWDG raw=0xXXXXXXXX
```

For a trial reset, the normalized result code is committed with the next
metadata record. For a confirmed slot, the event is retained in the
CRC-protected `.noinit` telemetry report and in the RSM/log event ring. The
ring is RAM-only and is therefore diagnostic across a warm reset, not a
power-loss durable audit log.

Reset flags are never used to treat an unverified image as safe. They only
inform trial accounting and diagnostics after the normal signature and vector
checks have passed.

