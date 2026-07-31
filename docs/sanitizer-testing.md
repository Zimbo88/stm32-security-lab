# Sanitizer testing

The C fuzz driver and existing host suites use fail-fast sanitizers. The
primary matrix is:

```text
AddressSanitizer + UndefinedBehaviorSanitizer
ASan leak detection where supported
UBSan halt-on-error with stack traces
```

Run it with:

```bash
make -C fuzz sanitize
make -C tests/host_verifier clean test SANITIZE=1
make -C tests/update_protocol clean test SANITIZE=1
make -C tests/update_storage clean test SANITIZE=1
```

The fuzz build also supports a separate `SANITIZERS` value, for example
`make -C fuzz clean all SANITIZERS=undefined`. GCC's `undefined` group covers
shift, alignment, bounds-related and signed-overflow checks applicable to these
modules; an `integer` sanitizer group is compiler-specific and is not claimed
without a successful compiler run. Sanitizers do not cover target-only
register code or physical flash behaviour.
