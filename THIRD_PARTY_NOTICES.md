# Third-Party Notices

## Monocypher 4.0.3

The bootloader uses the vendored Monocypher Ed25519 and SHA-512 implementation.
The source archive is `third_party/monocypher-4.0.3.tar.gz` and its expected
SHA-256 is recorded in `third_party/monocypher-4.0.3.tar.gz.sha256`.

Monocypher is dual-licensed under the 2-clause BSD license or CC0-1.0. The
upstream `LICENCE.md` is retained in the source archive. The repository uses
the files and local-copy notes listed in `third_party/MONOCYPHER_SOURCE.txt`.

## Python build and test dependencies

PyNaCl, pyserial, pytest, Hypothesis, coverage, Ruff, mypy and Bandit are
host-side dependencies pinned in `requirements.txt` and
`requirements-security.txt`. Their package metadata and license inventory are
recorded in [docs/licenses.md](docs/licenses.md). They are not target firmware
components.

No private signing material or hardware-capture data is third-party content
approved for redistribution.
