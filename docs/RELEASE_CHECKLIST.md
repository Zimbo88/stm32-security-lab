# Release Checklist

## Repository state

- [ ] Publication branch is clean.
- [ ] All intended source files are tracked.
- [ ] Generated files and local results are ignored.
- [ ] No private hostnames, user names, home paths, emails, or device identifiers remain.
- [ ] No private signing keys or signing seeds are tracked.
- [ ] Third-party license notices are preserved.

## Documentation

- [ ] README reflects the tested hardware and current architecture.
- [ ] Board image contains no private information.
- [ ] Hardware component descriptions match the actual board.
- [ ] English-language sweep completed.
- [ ] Threat model and limitations are current.
- [ ] HIL validation result is supported by curated evidence.
- [ ] `CITATION.cff` repository URL and release date are updated.

## Verification

- [ ] Host tests pass.
- [ ] Static checks pass.
- [ ] Bootloader builds from a clean tree.
- [ ] Application builds from a clean tree.
- [ ] Deterministic-build check passes.
- [ ] HIL campaign passes on real hardware.
- [ ] Restore verification passes.
- [ ] A second clean clone reproduces the documented build.

## Git and release

- [ ] Commit history has been reviewed for secrets and private data.
- [ ] Public history strategy has been chosen.
- [ ] Version is `1.0.0`.
- [ ] `CHANGELOG.md` release section is finalized.
- [ ] Annotated tag created.
- [ ] Release notes published.
- [ ] Source archive reviewed after publication.
