# Stack and memory budget

Target: STM32F429IGT6, 256 KiB supported main SRAM
(`0x20000000..0x20020000`). The application linker reserves 8 KiB at the top
of SRAM for the descending stack and a 256-byte MPU guard at its lower edge.
Static RAM must end at or below `_stack_guard_start`.

Normal Slot-B build measured during Part 2:

```text
text 21306, data 40, bss 1488, total 22834 bytes
```

The build emits `.su` files through `-fstack-usage`. The largest reported
single-frame value inherited from the earlier audit is 2256 bytes. This is not
a whole-path proof: nested UART, metadata, cryptographic, interrupt, and fault
paths still need call-graph analysis. The 8 KiB reserve is an engineering
budget, not a formal proof.

The bootloader/update path uses fixed buffers and no heap. Recursion is not
permitted in security-critical paths. `.noinit` retained fault/telemetry data
is included in the static-RAM linker bound. A future CI budget check should
fail if static RAM crosses the guard or measured worst-case usage exceeds the
reserve.

Status: linker reservation and MPU guard `IMPLEMENTED` and `HOST TESTED`; full
worst-case stack proof `DOCUMENTED ONLY`.
