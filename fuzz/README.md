# Host security fuzzing

This directory contains host-only fuzzing and property-testing assets for the
security-critical parsers. The harnesses call the production C parser,
metadata, package and boot-policy functions; they do not write a real STM32
flash device.

## Profiles

```text
make -C fuzz test       # bounded deterministic smoke campaign
make -C fuzz sanitize   # ASan/UBSan smoke campaign
make -C fuzz libfuzzer  # optional; requires clang with libFuzzer
```

The deterministic driver is portable and uses reviewed seed inputs with a
fixed LCG seed. It checks that production functions return normally. The
optional `libfuzzer` target uses LLVM's engine when `clang` is available.

Campaign outputs and crash findings belong under `fuzz/findings-local/` and
are ignored. Only reviewed, non-confidential regression inputs may be copied
to `tests/regressions/`.

| Mode | Production surface | Safety property |
| --- | --- | --- |
| `uart` | C UART frame parser | bounded state, no crash/OOB |
| `metadata` | redundant metadata decoder | invalid copies fail closed |
| `package` | package header and crypto wrapper | bounds precede verification |
| `policy` | boot-slot policy plus independent expected model | deterministic decision |

Python properties live in `tests/test_security_properties.py` and use
Hypothesis with bounded examples. Full cryptographic rejection is a normal
result; it is not treated as a crash finding.
