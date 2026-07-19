# Contributing

Contributions that improve reproducibility, documentation, test coverage,
hardware support, or defensive security analysis are welcome.

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

## Generated files

Do not commit compiler output, virtual environments, caches, temporary logs,
private signing material, or complete local HIL run directories.

Curated validation evidence may be added under a dedicated report directory
when it is anonymized, reproducible, and required to support a documented
result.

## Licensing

By submitting a contribution, you agree that it may be distributed under the
BSD 3-Clause License used by this repository.
