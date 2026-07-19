# Test Strategy

Tests are defined as typed data with stable identifiers. Each test includes:

- objective
- preconditions
- flash preparation actions
- required UART assertions
- forbidden UART assertions
- expected verdict
- tags
- timeout
- cleanup behavior

PASS requires explicit UART evidence. A reset alone is not evidence of success.

Negative security tests pass only when rejection evidence appears and application
execution evidence is absent.

Policy-dependent tests are classified as `OBSERVE` until the repository
documentation establishes a normative requirement. `OBSERVE` is not counted as
`PASS`. Use `--strict-observations` to make observations produce a non-zero exit.

The initial catalog covers:

- positive boot,
- authentication rejection,
- payload hash rejection,
- malformed signed-image headers,
- slot-policy observations,
- metadata redundancy observations,
- restore behavior,
- boot performance capture.

Host tests cover configuration parsing, region validation, process failures,
timeouts, immutable image mutations, deterministic campaigns, UART framing,
assertions, reporting, JUnit validity, build-cache survival, backup manifests,
restore idempotence, and exit-code rules.
