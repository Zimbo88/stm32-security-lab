# Fault handling

Exception entry selects the active MSP/PSP frame and stores core registers and
SCB fault status in a 104-byte `.noinit` record. A CRC32, magic, version, and
sequence validate the record after reboot. The handler performs no Flash write
and does not resume execution. `fault show` preserves the record; `fault clear`
invalidates it explicitly and is restricted in default EXP066 diagnostics.

The Runtime Security Monitor Phase 2A vector-table checks run from
`runtime_monitor_init()` and bounded `runtime_monitor_periodic()` steps only.
They do not run in the fault handler and do not change the retained fault-record
format.
