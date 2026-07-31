# Fuzzing

Fuzzing is host-only in this part. No harness sends arbitrary data to a real
board and no harness invokes a physical flash erase/program operation.

## Engines and harnesses

`fuzz/harnesses/host_fuzz_driver.c` links the production EXP045 C sources and
provides bounded modes for `uart`, `metadata`, `package` and `policy`. The
policy mode compares `boot_slot_selection_select()` with a smaller independent
decision model. The driver runs a fixed-seed mutation loop and executes the
original corpus once before mutation.

`fuzz/harnesses/libfuzzer_entry.c` is an optional LLVM libFuzzer entry point for
the parser surfaces. It is built only when `clang` with libFuzzer is present.
The current development environment has GCC and LLVM coverage tools, but no
`clang` executable, so no libFuzzer campaign is claimed here.

Seeds and dictionaries are reviewed, synthetic and free of private keys,
dumps and hardware logs. Local outputs belong under the ignored
`fuzz/findings-local/` directory.

## Commands

```bash
make -C fuzz clean all
make -C fuzz sanitize
python fuzz/scripts/run_campaign.py --iterations 10000
make fuzz
make -C fuzz libfuzzer       # optional; fails clearly if clang is unavailable
```

The portable engine is suitable for reproducible smoke and bounded campaigns.
It is not a substitute for coverage-guided libFuzzer/AFL++ runs. Each finding
must retain the harness, input, seed, compiler and sanitizer command before it
can become a checked-in regression.
