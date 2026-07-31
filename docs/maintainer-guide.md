# Maintainer Guide

## Routine work

1. Review the branch and tags without modifying existing releases.
2. Install the pinned Python dependencies in a virtual environment.
3. Run `make test-fast` and `make test-security`.
4. Review coverage gaps, fuzz results, static-analysis output and the feature
   evidence matrix.
5. Run the publication scan before sharing a patch or archive.

## Release-candidate work

Use an explicit test key for a local rehearsal:

```bash
make release-candidate RELEASE_TEST_KEY=1
make verify-release RELEASE_DIR=dist/v1.1.0-rc1-local
```

For an offline research or production-like key, pass
`RELEASE_SIGNING_SEED=/secure/path/key.seed`. The candidate records only its
public-key fingerprint. Never copy the seed into `dist/`, Git, an issue or an
artifact archive.

The candidate contains a sorted SHA-256 manifest, SPDX SBOM, provenance,
public-key material, firmware artifacts, package verification reports and a
deterministic archive. It is not a release until the checklist, remote CI and
hardware evidence are complete.

## Security and hardware handoff

Security reports use the private GitHub advisory channel when enabled. Public
issues must not contain keys, dumps, serial numbers or exploit details. RDP2,
WRP and Option Bytes remain manual, separately reviewed operations. A lost or
compromised signing key must be handled using the key-loss documentation and
must never be worked around by weakening the bootloader.

## Backups and ownership

Keep signing-key backups outside the repository with independent access
controls. Keep hardware baselines and raw HIL runs local unless they have been
anonymized and explicitly selected as publication evidence. A new maintainer
must receive the key fingerprint, layout profile, build instructions, test
reports, release checklist and known limitations together.
