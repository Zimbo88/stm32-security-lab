# Hardware Compatibility

Only rows with evidence in this table should be treated as supported.

| MCU/board | Flash | SRAM | Status | Evidence | Limitations |
|---|---:|---:|---|---|---|
| STM32F429IGT6-class development board | 1 MiB | 256 KiB device, 128 KiB supported application range | `HARDWARE VALIDATED` at RDP0 | Part 1/2 hardware reports and safe Part 3 UART regression | Board revision and pinout must match; RDP2/WRP not validated |
| STM32F429IGT6 with `stm32f429_1m` profile | 1 MiB | 256 KiB device | `IMPLEMENTED`, `HOST TESTED`, `HARDWARE VALIDATED` | Layout tests, firmware builds, documented board campaign | Uses sectors 0-11 and fixed PA9/PA10 USART1 wiring |
| STM32F429 2 MiB reference profile | 2 MiB | device-dependent | `IMPLEMENTED`, `HOST TESTED` only | Generated layout and host tests | No current hardware evidence; not a release target |
| Other STM32F4 devices or boards | varies | varies | `NOT IMPLEMENTED` as a support claim | None | Do not infer compatibility from MCU family names |

Before using another board, confirm exact MCU ID, flash size, sector map,
vector address, SRAM acceptance, clocking, UART pins, reset wiring and power
levels. A successful compile is not hardware validation.
