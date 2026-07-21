# Platform feature matrix

Status values are intentionally limited to:
`TARGET_IMPLEMENTED`, `HOST_TOOL_IMPLEMENTED`, `SIMULATED_ONLY`,
`DOCUMENTED_DESIGN`, `NOT_IMPLEMENTED`, `HARDWARE_VALIDATION_REQUIRED`, and
`OPTIONAL_NOT_SELECTED`.

| Feature | Status | Relevant source files | Relevant tests | Limitations | Next concrete action |
|---|---|---|---|---|---|
| bootloader | TARGET_IMPLEMENTED | `firmware/exp045_bootloader_v2` | EXP045 build/report | Hardware recovery is still incomplete. | Hardware boot validation on expendable board. |
| secure firmware update | DOCUMENTED_DESIGN | `firmware/exp045_bootloader_v2`, EXP065 signer | EXP045/EXP065 builds | No target-side update receiver/installer. | Define authenticated recovery/update flow. |
| UART update | NOT_IMPLEMENTED | none | none | No UART update protocol or Flash writer. | Design slot-based update protocol before coding. |
| optional USB update | OPTIONAL_NOT_SELECTED | none | none | USB stack not selected for this release. | Select only after hardware USB validation plan. |
| CRC | TARGET_IMPLEMENTED | EXP045/EXP067/EXP070 helpers | `tests/test_exp067.py`, `tests/test_exp070.py` | CRC is integrity/error detection, not authentication. | Keep paired with signatures/hashes. |
| version management | TARGET_IMPLEMENTED | EXP045 policy, EXP067 package header, EXP070 catalog | EXP045 build, EXP067/EXP070 tests | Bootloader rollback floor is compiled, not persistent. | Add persistent target policy only with update manager. |
| anti-rollback | TARGET_IMPLEMENTED | EXP045 image policy, EXP070 simulator | EXP045 build, `tests/test_exp070.py` | EXP070 module rollback is simulated only. | Hardware-backed monotonic storage design. |
| signature verification | TARGET_IMPLEMENTED | EXP045 verifier, EXP067 parser | EXP045 build, `tests/test_exp067.py` | Target verifies firmware image, not module packages. | Target module verifier only after slot design. |
| RCC diagnostics | TARGET_IMPLEMENTED | `firmware/exp066_research_platform_core/src/platform.c` | EXP066 build, `tests/test_exp066_pure.py` | Read-only curated snapshot only. | UART hardware validation. |
| GPIO diagnostics | TARGET_IMPLEMENTED | `platform.c`, `platform_led.c` | EXP066 build | GPIO diagnostics are curated; no arbitrary GPIO writes. | Validate LED pins/polarity on hardware. |
| NVIC diagnostics | TARGET_IMPLEMENTED | `platform.c` | EXP066 build | Only bank-0 curated read-only registers. | Expand only after interrupt inventory. |
| SCB diagnostics | TARGET_IMPLEMENTED | `platform.c`, `fault.c` | EXP066 build | Curated fault/status registers only. | Validate fault snapshots on hardware. |
| SysTick diagnostics | TARGET_IMPLEMENTED | `platform.c` | EXP066 build | Read-only; SysTick is not configured as scheduler. | Decide whether to enable real tick source. |
| MPU diagnostics | TARGET_IMPLEMENTED | `platform.c` | EXP066 build | Read-only status, no MPU policy enforcement. | Hardware MPU isolation experiment. |
| FLASH register diagnostics | TARGET_IMPLEMENTED | `platform.c` | EXP066 build | Read-only curated registers only. | Decode fields after reference manual review. |
| DBGMCU diagnostics | TARGET_IMPLEMENTED | `platform.c` | EXP066 build | Read-only IDCODE only. | Validate expected device ID on hardware. |
| PWR diagnostics | NOT_IMPLEMENTED | none | source allowlist tests | PWR clock/register sampling not selected. | Add only with clock-safe read policy. |
| SYSCFG diagnostics | NOT_IMPLEMENTED | none | source allowlist tests | SYSCFG clock/register sampling not selected. | Add only with clock-safe read policy. |
| bounded memory diagnostics | TARGET_IMPLEMENTED | `platform.c` | EXP066 build | Region reporting only; no memory peek/poke. | Add fixed read-only snapshots only if justified. |
| RAM diagnostic snapshots | NOT_IMPLEMENTED | none | none | RAM tests are placeholders. | Define bounded RAM test that avoids data loss. |
| defined Flash-region reading | NOT_IMPLEMENTED | none | source allowlist tests | No Flash content read command. | Add only predefined immutable hashes/ranges. |
| optional EEPROM emulation | OPTIONAL_NOT_SELECTED | none | none | STM32F429 has no EEPROM; emulation not selected. | Revisit only with wear-level design. |
| UART logging | TARGET_IMPLEMENTED | `log.c`, `log.h`, `platform.c` | EXP066 build | RAM-only log, lost on reset. | Persistent log only after update journal design. |
| ring buffer | TARGET_IMPLEMENTED | `log.c` | `tests/test_exp066_pure.py` | Fixed 32-record RAM buffer. | Hardware CLI validation. |
| error history | TARGET_IMPLEMENTED | `log.c`, `fault.c` | EXP066 build | RAM log plus retained fault record only. | Add persistent history only outside fault context. |
| boot log | TARGET_IMPLEMENTED | `platform.c`, `log.c` | EXP066 build | Boot log is RAM-only. | Validate reset entries over UART. |
| reset causes | TARGET_IMPLEMENTED | `platform.c`, EXP045 reset helper | EXP066 build | EXP066 captures raw `RCC_CSR`; decoding is minimal. | Add decoded fields after hardware reset campaign. |
| HardFault dump | TARGET_IMPLEMENTED | `fault.c` | EXP066 build | Retained `.noinit` only; no Flash persistence. | Validate induced fault on expendable hardware. |
| GPIO tests | DOCUMENTED_DESIGN | `platform.c` | source allowlist tests | CLI returns bounded placeholder PASS only. | Implement non-destructive pin-state tests. |
| timer tests | NOT_IMPLEMENTED | none | none | No timer driver/test. | Add after timer inventory. |
| DMA tests | NOT_IMPLEMENTED | none | none | No DMA driver/test. | Add after DMA safety plan. |
| interrupt tests | NOT_IMPLEMENTED | none | none | No interrupt self-test harness. | Add after NVIC/ISR inventory. |
| watchdog tests | NOT_IMPLEMENTED | none | none | Watchdog is not enabled by EXP066. | Add on expendable board with recovery plan. |
| clock tests | DOCUMENTED_DESIGN | `platform.c` | source allowlist tests | CLI placeholder only; no clock switching. | Add read-only clock consistency checks. |
| device information | TARGET_IMPLEMENTED | `platform.c`, `runtime_monitor.c` | EXP066 build | Public ID/revision/UID fingerprint; raw UID is restricted. | Validate values on board. |
| UID | TARGET_IMPLEMENTED | `platform.c`, `runtime_monitor_core.c` | RSM host tests, EXP066 build | Public CRC32 fingerprint only; raw UID restricted. | Hardware output capture. |
| Flash size | TARGET_IMPLEMENTED | `platform.c` | EXP066 build | Raw factory flash-size register. | Hardware output capture. |
| read-only option-byte reporting | TARGET_IMPLEMENTED | `platform.c` | EXP066 build | Raw `FLASH_OPTCR` is restricted; no writes or full decode. | Decode safely after review. |
| boot configuration | TARGET_IMPLEMENTED | `platform.c` | EXP066 build | Reports Stage-0 launch assumption, no boot mailbox. | Add authenticated boot-result mailbox if needed. |
| runtime information | TARGET_IMPLEMENTED | `platform.c` | EXP066 build | Version, health, LED mask, reset, and self-test state only. | Add uptime units after real tick source. |
| runtime vector-table monitor | TARGET_IMPLEMENTED | `runtime_monitor_vector.c`, `runtime_monitor.c` | RSM host tests, EXP066 build | Checks VTOR, MSP, core handlers, reserved entries, Thumb bits, and active-slot Flash range; no reset or recovery action. | Hardware UART capture and controlled vector test hook. |
| UART CLI | TARGET_IMPLEMENTED | `platform.c`, `uart.c` | `tests/test_exp066_pure.py` | Blocking TX remains; RX is bounded. | Hardware CLI soak test. |
| binary protocol | NOT_IMPLEMENTED | none | none | Text CLI only. | Design framed protocol separately. |
| optional USB CDC | OPTIONAL_NOT_SELECTED | none | none | USB not selected. | Add only after USB hardware/clock plan. |
| optional Ethernet | OPTIONAL_NOT_SELECTED | none | none | Ethernet not selected. | Add only if PHY wiring is confirmed. |
| LCD | NOT_IMPLEMENTED | none | none | No LTDC/LCD driver. | Hardware inventory and safe driver plan. |
| SDRAM | NOT_IMPLEMENTED | none | none | External SDRAM not initialized or trusted. | FMC/SDRAM experiment before use. |
| gyroscope | NOT_IMPLEMENTED | none | none | No SPI/I2C/driver work. | Confirm sensor wiring and bus. |
| signed bytecode modules | HOST_TOOL_IMPLEMENTED | `tools/bytecode_vm.py`, `tools/bytecode_asm.py` | `tests/test_exp068.py` | Not executed on target firmware. | Target VM only after memory/scheduler plan. |
| constrained signed native modules | HOST_TOOL_IMPLEMENTED | `tools/native_loader.py` | `tests/test_exp069.py` | Validator/simulator only; no target execution. | MPU-isolated target design and hardware tests. |
| atomic module installation | SIMULATED_ONLY | `tools/module_install.py` | `tests/test_exp070.py` | JSON Flash simulation, not real Flash. | Hardware-safe target slot/journal implementation. |
| rollback and quarantine | SIMULATED_ONLY | `tools/native_loader.py`, `tools/module_install.py` | EXP069/EXP070 tests | Host lifecycle/catalog simulation only. | Hardware-backed failure counters and rollback. |
| LED platform health indication | TARGET_IMPLEMENTED | `platform_health.c`, `platform_led.c`, `platform.c` | `tests/test_exp071_health.py`, EXP066 build | Hardware LED polarity still needs validation. | UART/LED hardware validation. |
| optional retro audio | OPTIONAL_NOT_SELECTED | `platform_audio.c`, `platform_audio.h` | `tests/test_exp071_health.py` | No documented speaker/buzzer pin. | Add external piezo pin assignment before implementation. |
