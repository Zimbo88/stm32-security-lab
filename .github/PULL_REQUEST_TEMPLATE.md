## Summary

Describe the technical purpose of this change.

Link the relevant issue or ADR and state whether this changes a security
claim, memory layout, protocol, firmware format or release behavior.

## Security impact

- [ ] No security-relevant behavior changes
- [ ] Image verification
- [ ] Key handling
- [ ] Metadata or slot selection
- [ ] Rollback policy
- [ ] Update or recovery
- [ ] Flash protection or option bytes
- [ ] HIL or flashing behavior

Explain the impact and residual risk:

## Validation

- [ ] Host tests passed
- [ ] Static checks passed
- [ ] Firmware builds passed
- [ ] HIL tests passed or are not required
- [ ] Documentation updated
- [ ] Diff reviewed for private information
- [ ] Generated artifacts excluded
- [ ] Changelog updated when user-visible
- [ ] Publication scan and private-key scan passed
- [ ] `git diff --check` passed

Commands and results:

## Hardware

Target board, MCU revision, probe, and serial setup used for validation:

## Checklist

- [ ] The change fails safely
- [ ] Negative tests are included where relevant
- [ ] No private signing material is included
- [ ] No flash dump, raw hardware log or device serial is included
- [ ] No Option-Byte, RDP or WRP write automation is added
- [ ] Public text is written in English
