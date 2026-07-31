# Versioning

Version values describe different compatibility domains and must not be
silently substituted for one another.

| Value | Meaning | Change policy |
|---|---|---|
| Project version | Documentation and tooling release identity, e.g. `1.1.0-rc1-local` | Semantic Versioning for repository releases |
| Bootloader version | Stage-0 implementation identity | Changes require separate bootloader validation and are not an application update |
| Application image version | Monotone firmware version in the signed manifest | Must advance above the confirmed rollback floor |
| Update-package format | Serialized package/header contract, currently v2 | Incompatible changes require a new format and verifier policy |
| Manifest format | Signed manifest encoding, currently v1 for images and v2 for update packages | Changes require explicit parser compatibility review |
| UART protocol | `SUPD` frame protocol, currently version 1 | Version changes require host and target compatibility tests |
| Metadata format | Redundant boot metadata record format, currently version 1 | Changes require recovery and corruption-matrix evidence |
| Security epoch | A documented policy boundary for future key or verifier changes | No hardware monotonic counter exists in this baseline |
| Rollback floor | Confirmed firmware version stored by Stage 0 | Software-backed; it is not hardware tamper resistant |

Major changes may break image, package or protocol compatibility. Minor changes
add compatible behavior where the verifier and host retain old behavior. Patch
changes are intended for compatible fixes and documentation. Research builds
must use an explicit suffix and test key. A hardware-validated release is not
implied by a project version or tag alone; its evidence is recorded separately.

Git tags identify repository snapshots. Firmware image versions identify boot
policy state. A tag and firmware version should be cross-referenced in the
release manifest, but they are not interchangeable.
