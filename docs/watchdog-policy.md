# Independent watchdog policy

Status: IMPLEMENTED in software, HOST POLICY TESTED, hardware validation
recorded separately. No IWDG option byte is changed.

The application starts the STM32F429 independent watchdog after UART,
platform self-tests and RSM initialization. It uses the independent LSI clock:

| Parameter | Value |
|---|---:|
| IWDG prescaler register | 4 (`/64`) |
| reload | 2000 |
| nominal LSI assumption | 32 kHz |
| nominal timeout | about 4002 ms |
| documented LSI range | 17–47 kHz |
| resulting timeout range | about 2724–7533 ms |

The watchdog is refreshed once at the end of each bounded `platform_idle`
unit, after monitor, CLI, scenario and confirmation work. It is not refreshed
inside an unbounded loop. A fault handler, watchdog-hang test scenario, blocked
main loop or stalled application therefore eventually produces an IWDG reset.

The IWDG configuration uses the hardware key sequence and bounded status
polling. If the prescaler/reload registers never become ready, the platform
marks initialization as a critical failure and cannot pass the health gate.
The watchdog is not disabled by the debugger and is not configured by option
bytes.

The application IWDG remains running across a software reset into Stage-0.
Stage-0 refreshes it at boot, during the UART handshake and between bounded
update/flash/hash operations. A single STM32F4 sector erase must therefore
complete within the selected watchdog window; this is a documented hardware
assumption, not a debugger workaround. The UART updater remains bounded by
protocol timeouts. The application watchdog timeout is long enough for normal
UART diagnostics and the bounded health gate, but the LSI tolerance must be
confirmed on the actual device.

An IWDG reset sets `RCC_CSR.IWDGRSTF`. Stage-0 emits `BOOT_RESET
cause=IWDG`, and a pending trial stores `RESET_CAUSE_RESULT_IWDG` in the
metadata result field. A confirmed slot reports the reset through telemetry
and RSM but remains confirmed.
