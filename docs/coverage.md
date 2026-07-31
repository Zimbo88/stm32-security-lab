# Coverage

Coverage is measured separately for project policy/parser code and vendored
cryptography. The latter is not used as a quality gate for this repository;
the wrapper and call-site coverage are the relevant security evidence.

## C

```bash
python tools/collect_c_coverage.py
```

This runs `tests/update_protocol` and `tests/update_storage` with GCC gcov,
including branch counters, then writes ignored JSON/Markdown under
`coverage/`. The report excludes test drivers, simulated-flash helpers,
performance stubs and Monocypher from the project aggregate. It still retains
per-file data so reviewers can see untested paths.

The repository's target goals are 90% line and 85% branch coverage for own
security modules, with 95%/90% aspirations for small policy modules. These are
engineering targets, not security proofs. Current baseline coverage is
reported by the generated file and is intentionally not rounded up to meet a
target.

The executed C report measured the following project-source aggregates
(Monocypher, test drivers, simulated flash and stubs excluded):

| Suite | Project line | Project branch | Critical parser/policy line | Critical parser/policy branch |
|---|---:|---:|---:|---:|
| `tests/update_protocol` | 65.03% | 75.40% | 67.59% | 71.32% |
| `tests/update_storage` | 77.27% | 89.26% | 76.68% | 88.12% |

The update-protocol suite therefore remains below the stated target. The
missing branches are recorded as test debt rather than hidden with coverage
exclusions.

## Python

```bash
python -m coverage run --branch -m pytest -q -p no:cacheprovider tests
python -m coverage report -m
```

`.coveragerc` restricts the report to repository Python tools and enables
branch measurement. Third-party HIL and deliberately hardware-dependent
helpers are excluded with an explicit rationale.

The executed Python report was 2,840 own-tool statements, 33% aggregate line
coverage and 902 measured branches. `tools/stm32ctl/protocol.py` reached 84%
line coverage; the aggregate is lower because command-line tools and hardware
helpers are intentionally not all invoked by the unit suite. These numbers are
baseline evidence, not a claim that the recommended 85/90% targets have been
met.
