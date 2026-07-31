# Static analysis

`tools/run_static_analysis.py` runs GCC 13's `-fanalyzer` on the own C parser,
metadata, slot-policy and flash-boundary modules. Vendored Monocypher is
excluded from this project-specific analyzer invocation and is tracked as a
separate external dependency.

Optional tools are detected rather than silently simulated:

```bash
python tools/run_static_analysis.py
python tools/run_static_analysis.py --strict  # fail if optional tools are missing
```

The current environment executed GCC `-fanalyzer` successfully. `cppcheck`,
`clang-tidy` and `scan-build` were not installed, so their absence is recorded
in the JSON result and is not called a clean multi-tool analysis. No global
warning suppression was added.
