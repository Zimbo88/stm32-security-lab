# Release Artifacts

A reviewed release candidate is a directory and archive, not just a firmware
binary. The local candidate produced by `make release-candidate` contains:

- EXP045 bootloader ELF, BIN and HEX;
- EXP066 Slot A and Slot B ELF, BIN and HEX;
- signed Slot A and Slot B update packages and their build/inspect/verify JSON;
- legacy EXP065 signed-image artifacts where the compatibility build is used;
- a public-key file and SHA-256 fingerprint;
- `release-manifest.json` with schema, commit, dirty status, layout, versions,
  sizes and hashes;
- `sbom.spdx.json` and its SHA-256 hash;
- `release-provenance.json` with commands, tools, inputs and outputs;
- `SHA256SUMS`, marker evidence, notices and a changelog excerpt.

Private seeds, private keys, local logs, dumps, caches, absolute paths and
hardware serial numbers are not release artifacts. A test-key candidate is
labelled `TEST-ONLY` in the manifest and is never a production approval.

`make verify-release RELEASE_DIR=...` checks the expected file set, hashes,
SBOM, provenance, package signatures, public-key fingerprint, layout metadata,
marker report, archive and publication scan. The check is offline after the
Python dependencies are installed.
