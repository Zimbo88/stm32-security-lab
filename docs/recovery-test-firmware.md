# Controlled recovery test firmware

Status: IMPLEMENTED as explicit build scenarios; not part of the normal
release build.

The application Makefile accepts a scenario token. Each scenario is compiled
into a separate build directory by `test-scenario` and is not selected by the
default `TEST_SCENARIO=normal` build.

```bash
make -C firmware/exp066_research_platform_core SLOT=b \
  TEST_SCENARIO=trial_watchdog_hang test-scenario
```

The update package must be signed separately with the lab test signing seed
and an explicit version, for example:

```bash
make -C firmware/exp066_research_platform_core \
  SLOT=b TEST_SCENARIO=trial_no_confirm \
  BUILD=build/test_trial_no_confirm \
  PROJECT=exp066_trial_no_confirm IMAGE_VERSION=100 \
  SIGNING_SEED=/absolute/path/to/lab-test-seed.bin \
  update-package
```

The seed path is never copied into the repository or printed by the build.

| Scenario | Deterministic behavior | Expected policy |
|---|---|---|
| `trial_success` | normal application loop | health gate and confirmation |
| `trial_no_confirm` | health gate confirmation disabled | bounded trial then fallback |
| `trial_hardfault` | invalid access at loop 10 | retained fault, IWDG reset, fallback |
| `trial_watchdog_hang` | unbounded loop at loop 10 | IWDG reset, fallback |
| `trial_software_reset` | `SYSRESETREQ` at loop 10 | software reset result, fallback |
| `trial_health_fail` | application health predicate false | no confirmation, bounded trial |
| `trial_invalid_vector` | reset vector is `0xFFFFFFFF` | signed package rejected before boot |
| `trial_delayed_confirm` | confirmation held until loop 100 | trial remains pending, then confirms |

Every reachable scenario prints `TEST_SCENARIO active` and a unique event
line. `trial_invalid_vector` is intentionally rejected by Stage-0 and thus
does not reach application UART output. These artifacts must never be used as
normal release images.

