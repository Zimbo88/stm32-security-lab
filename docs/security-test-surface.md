# Security test surface

This inventory maps externally controlled input to a concrete, host-executable
test method. Host fuzzing never writes a physical STM32 flash device.

| Component | Input | Trust | Main failure modes | Test method |
|---|---|---|---|---|
| UART frame parser | arbitrary bytes | untrusted | desynchronisation, length/OOB, CRC, hangs | C deterministic fuzzing and optional libFuzzer |
| Update package header | package bytes | untrusted | truncation, trailing data, overflow, reserved fields | C package harness, Python properties |
| Manifest | package bytes | untrusted | version, target, flags, vector and size errors | C package/full verification harness and vectors |
| Signed image | flash/package bytes | untrusted | hash/signature/vector/bounds errors | C full wrapper and existing verifier tests |
| Boot metadata | two 128-byte flash copies | damageable state | CRC, commit, generation ambiguity, invalid status | C metadata fuzzing and corruption tests |
| Slot policy | metadata plus reset state | untrusted state | wrong slot, trial loop, fallback decision | C policy harness with independent expected model |
| `stm32ctl` response parser | UART response bytes | untrusted device | malformed response, enum/length errors | Hypothesis and existing negative tests |
| Release/update JSON | local files | untrusted file | wrong types, paths, hashes and versions | Hypothesis, Python tests, coverage |
| Key tooling | seed/header paths | local input | permissions, overwrite, leakage | negative tests and private-key scan |
| Memory layout helpers | JSON/config integers | repository input | address+size overflow, bad sector coverage | existing layout tests and property tests |
| MPU descriptors | build-time policy values | internal input | alignment, size and region overlap | C host policy tests |

The highest-value review surfaces are the UART parser, package bounds before
erase/program, signed-image verification, metadata selection and the
transition from candidate to trial. Cryptographic rejection is an expected
return value, not a fuzzing crash.
