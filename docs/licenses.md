# License Inventory

The repository source is distributed under the BSD 3-Clause License in
`LICENSE`.

| Component | Version | License | Evidence/source |
|---|---|---|---|
| STM32 Security Lab | current checkout | BSD-3-Clause | `LICENSE` |
| Monocypher | 4.0.3 | dual 2-clause BSD or CC0-1.0 | vendored archive `LICENCE.md`, `third_party/MONOCYPHER_SOURCE.txt` |
| PyNaCl | 1.5.0 | Apache-2.0 | pinned in `requirements.txt`; package metadata |
| pyserial | 3.5 | BSD-3-Clause | pinned in `requirements.txt`; package metadata |
| pytest | 7.4.4 | MIT | pinned in `requirements.txt`; package metadata |
| Hypothesis | 6.131.9 | MPL-2.0 | pinned in `requirements.txt`; package metadata |
| coverage | 7.6.12 | Apache-2.0 | pinned in `requirements-security.txt`; package metadata |
| Ruff | 0.16.1 | MIT | pinned in `requirements-security.txt`; package metadata |
| mypy | 2.3.0 | MIT | pinned in `requirements-security.txt`; package metadata |
| Bandit | 1.9.4 | Apache-2.0 | pinned in `requirements-security.txt`; package metadata |

The Python entries are test/build dependencies, not firmware code linked into
the target. The SBOM generator records the exact requirement pins and the
source archive hash for Monocypher. When a dependency is upgraded, update the
pin, this inventory and the generated SBOM together.
