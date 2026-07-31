# Research key directory

This directory intentionally contains no private key material.

The repository's test and research keys are generated locally with
`tools/key_management.py`. Files ending in `.seed` and `keys/*.bin` are ignored
and private seeds must have mode `0600`. Never use a CI, developer, or
research-device key as a production signing key.

| Class | Purpose | Production use |
| --- | --- | --- |
| `ci-test` | Automated parser and signature tests | Never |
| `developer-test` | Local development images | Never |
| `research-device` | Reversible lab-device validation | Never |
| `production-like` | Offline process rehearsal | Only after an independent ceremony |

The embedded public key is public, but its fingerprint must be compared with
the documented device baseline before signing or provisioning. A private seed
must never be committed, copied into a release artifact, or printed to logs.
