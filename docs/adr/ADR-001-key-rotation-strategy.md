# ADR-001: one embedded root key for the fixed research platform

Status: accepted for Part 2.

## Decision

Use Variant A: Stage 0 contains exactly one Ed25519 public key and no in-field
key rotation. New keys require a new Stage-0 image and a controlled
pre-RDP/manufacturing re-provisioning operation.

Two pre-provisioned keys reduce single-key loss impact but require key IDs,
revocation semantics, and persistent selection policy. A signed key-manifest
change or root-plus-release-key hierarchy adds epochs, atomic storage, recovery
complexity, and a larger Stage-0 audit surface. The current fixed layout has no
hardware monotonic counter and no spare authenticated journal dedicated to key
state.

One key is therefore the lowest-risk coherent choice for this research layout,
not a claim that it is ideal for production. A future rotation design must
provide authenticated epochs, atomic storage, migration for existing images,
and full power-loss testing. No UART-triggered rollback or research backdoor is
added; older images are tested on separately provisioned pre-RDP boards.
