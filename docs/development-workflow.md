# Development Workflow

`main` is the intended stable integration branch. Work is performed on focused
feature, security-test or documentation branches and merged only after the
relevant host evidence and review are complete. Release candidates are local
directories until CI, hardware evidence and maintainer review are complete.

Parts 1-3 are represented by the ancestor commits of
`feature/root-of-trust-hardening` and `test/security-fuzzing-and-coverage`.
Part 4 is developed on `chore/open-source-release-readiness`; no remote branch
is changed here. Future integration should preserve the focused commits and
review each security-sensitive change rather than creating an opaque merge.

Before a pull request, run:

```bash
make test-fast PYTHON=python
make test-security PYTHON=python
make release-candidate RELEASE_TEST_KEY=1
make verify-release RELEASE_DIR=dist/v1.1.0-rc1-local
```

Use `make fuzz` for the longer host-only campaign. Hardware changes additionally
require the relevant documented procedure, anonymized evidence and an explicit
statement of what was not tested.

Commit subjects should be short and imperative, for example
`release: add deterministic candidate manifest`. Existing tags are immutable;
new tags are created only after a reviewed candidate has passed its checklist.
