# Threat model

## Protected values

Protected values are Stage 0, its embedded Ed25519 public key, confirmed slot
images, redundant boot metadata and rollback floor, runtime security events,
authenticated update packages, research markers, and the public device
identity fingerprint. Private signing seeds are operational secrets rather
than device-readable values.

## Attacker capabilities

The model covers arbitrary UART bytes, malformed or replayed packages, old but
correctly signed images, corrupted metadata, a compromised application image,
faulty signed firmware, public knowledge of protocols and addresses, and
physical access while RDP is disabled. It records the future RDP2 case, where
debug and ROM-bootloader recovery are unavailable, and the consequences of a
lost or stolen signing seed.

The model does not assume that a signature makes faulty code safe. A correctly
signed image can still exhaust the trial budget, fault, or fail health checks.

## Out of scope for Part 2

Invasive analysis, decapping, laser or electromagnetic injection, voltage or
clock glitching, laboratory side channels, and a physical power-loss campaign
are not tested here. Power-loss testing is a separate campaign and remains
open.

## Security claims and evidence

| Claim | Implementation/evidence | Assumption or limit |
|---|---|---|
| Only authorized firmware boots | manifest, hash, Ed25519, vector and version checks | Stage 0 and public key are trusted |
| Only authorized firmware installs | same checks before erase, then readback and commit | software policy until WRP/RDP |
| Old firmware is rejected | minimum version plus confirmed metadata floor | no hardware monotonic counter |
| Stage 0 is not a normal update target | restricted write regions; negative tests | not physically WRP-protected |
| Metadata ambiguity fails closed | CRC, commit markers, canonical-state validation | corruption simulation is not power-loss evidence |
| Recovery does not bypass security | recovery uses the same signed verification | requires the valid signing key |
| Application writes are constrained | MPU read-only region plus confirmation exception | application is privileged |
| Test/production keys are distinct | key tool, labels, offline process | operators must verify fingerprints |

## Residual high-impact attacks

An attacker with the private signing seed can authorize arbitrary code. A
compromised privileged application can disable the MPU or call the existing
metadata confirmation primitive. Corruption of Stage 0, both metadata copies,
or both slot images can require signed recovery or pre-RDP SWD repair. RDP2
does not repair software defects and removes ordinary repair paths.
