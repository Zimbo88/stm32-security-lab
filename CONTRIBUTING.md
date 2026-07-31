# Contributing

This project is currently maintained by Mathias Zimmermann.

Bug reports, technical discussions and contributions are welcome.

Contributions that improve reproducibility, documentation, test coverage,
hardware support, or defensive security analysis are welcome.

Read [the project scope](docs/project-scope.md), [the quickstart](docs/quickstart.md)
and [the development workflow](docs/development-workflow.md) first. This is a
research reference, not a production-certified boot chain.

## Principles

Changes should:

- preserve safe-failure behavior;
- avoid weakening verification, recovery, or rollback checks;
- remain reproducible on documented hardware;
- include tests for security-relevant behavior;
- use English for source comments, documentation, reports, and user-facing output;
- avoid personal paths, hostnames, credentials, and private device data;
- explain the technical reason for the change.

## Development workflow

1. Create a focused branch.
2. Keep each change limited to one technical purpose.
3. Run the relevant host tests and static checks.
4. Run hardware-in-the-loop tests when firmware or flashing behavior changes.
5. Update documentation when observable behavior changes.
6. Review the diff for private information and generated artifacts.

## Commit messages

Use concise imperative commit subjects, for example:

- `Add manifest boundary validation`
- `Document HIL restore guarantees`
- `Reject invalid application vector tables`

Avoid vague subjects such as `fix`, `update`, or `changes`.

## Security-sensitive changes

Changes to image verification, key handling, metadata selection, rollback
policy, flash protection, recovery, or update installation require:

- a description of the threat being addressed;
- negative tests;
- safe-failure verification;
- documentation of residual risk.

Do not use public issues for vulnerability details. Follow `SECURITY.md` and
GitHub Private Vulnerability Reporting when enabled. Never attach private
keys, signing seeds, flash dumps, raw UART captures, ST-Link serial numbers or
local HIL directories.

## Evidence labels

Use `HOST TESTED` only for a reproducible host command, `HARDWARE VALIDATED`
only for observed documented hardware evidence, and `security reviewed` only
when a maintainer has explicitly reviewed the relevant threat and diff. A
successful build alone is not hardware or security evidence.

## Pull requests and sign-off

Pull requests must complete the repository template, add a changelog entry for
user-visible behavior, and include the exact validation commands. The project
does not currently require a DCO sign-off; authorship and license remain
covered by the BSD 3-Clause project license and normal repository review.

## Generated files

Do not commit compiler output, virtual environments, caches, temporary logs,
private signing material, or complete local HIL run directories.

Curated validation evidence may be added under a dedicated report directory
when it is anonymized, reproducible, and required to support a documented
result.

## Licensing

By submitting a contribution, you agree that it may be distributed under the
BSD 3-Clause License used by this repository.
