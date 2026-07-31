# Key management

The device currently has one embedded Ed25519 verification key. The matching
private seed is not part of the repository. `tools/key_management.py` provides
non-overwriting generation and public fingerprint inspection.

Current embedded public-key fingerprint (SHA-256 over the raw 32-byte key):

```text
482dd9daac3d406f779995a50a00eb2ac9948eb412cba4403e2f81091780499a
```

| Class | Use | Committed private material | Production use |
|---|---|---:|---:|
| CI test | automated parser/signature tests | never | No |
| developer test | local images | never | No |
| research device | reversible lab board | never | No |
| production-like offline | ceremony rehearsal | never | only after review |

```bash
python3 tools/key_management.py generate \
  --purpose research-device \
  --seed /secure/lab/stm32-research.seed \
  --public-header /secure/lab/research-public-key.h
python3 tools/key_management.py inspect --seed /secure/lab/stm32-research.seed
```

Seed paths must be mode 0600 and existing files are refused. The tool emits
only the public key and its SHA-256 fingerprint. `update_package.py build`
derives the public key in memory, adds the fingerprint to its build report,
and immediately verifies the generated package. The seed is never in a
package, manifest, UART frame, or log.

Before a device update, compare the fingerprint against the embedded header
and release manifest. Keep at least two encrypted offline backups under
separate control, with fingerprint, purpose, creation record, and
test/production classification.

No in-field key rotation or revocation is implemented. See the ADR and the
key-loss response. This is deliberate: a second key manifest without a
hardware monotonic counter would increase brick and rollback risk.
