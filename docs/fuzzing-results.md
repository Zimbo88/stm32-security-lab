# Fuzzing results

Results in this document are claims only for commands actually run on the
current checkout. The campaign driver records the mode, iteration count,
corpus size, seed and process output in JSON.

## Smoke evidence

`make -C fuzz sanitize` completed with ASan and UBSan for all four modes:

```text
uart     1,000 iterations  FUZZ_OK
metadata 1,000 iterations  FUZZ_OK
package  1,000 iterations  FUZZ_OK
policy     100 iterations   FUZZ_OK
```

The Python campaign runner also completed all four modes with 1,000 iterations
and a generated, deterministic test-signed package seed. No crash, timeout,
OOM or sanitizer finding was produced.

## Extended bounded campaign

`make fuzz` completed on this checkout with the portable deterministic engine.
The result JSON is local and ignored under `fuzz/findings-local/`.

| Harness | Executions | Corpus bytes | Duration | Result |
|---|---:|---:|---:|---|
| UART | 1,000,000 | 29 | 0.335 s | `FUZZ_OK` |
| metadata | 1,000,000 | 83 | 0.128 s | `FUZZ_OK` |
| boot policy model | 1,000,000 | 83 | 0.949 s | `FUZZ_OK` |
| package/full wrapper | 10,000 | 640 | 0.016 s | `FUZZ_OK` |

The package run uses a deterministic test-signed seed and the real package
verification wrapper. Its lower count is the documented exception for the
slower cryptographic path; most mutated inputs are rejected structurally.
No crash, timeout, OOM or sanitizer finding was produced.

## Findings

No reproducible memory-safety or undefined-behaviour finding has been observed
in the executed smoke campaigns. This is evidence about these bounded runs,
not a proof of parser safety. No crash artifact or local hardware data is
checked in.
