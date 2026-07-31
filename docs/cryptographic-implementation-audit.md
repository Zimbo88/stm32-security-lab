# Cryptographic implementation audit

Stage 0 uses vendored Monocypher 4.0.3 Ed25519 and SHA-512 sources. The source
archive is `third_party/monocypher-4.0.3.tar.gz` with SHA-256
`a7cbae546fbdc489bca632c3747e1ceb8ca3d4bd39e2706a0916f28ccd280e50`.
`MONOCYPHER_SOURCE.txt` records the upstream release and CC0/2-clause BSD
licensing. The four required source/header files are byte-identical to the
vendored release tree; no local cryptographic source delta was found.

The application does not implement new cryptography. CRC32 is error detection
and metadata/event integrity, not authentication.

The verifier checks fixed-width manifest fields, canonical flags and reserved
words, exact payload capacity, checked address arithmetic, SHA-512 over exactly
`image_size`, Ed25519 over the exact 96-byte manifest, and vector/MSP/reset
ranges. The installer repeats package checks before erase, then verifies
readback and the installed image before committing candidate metadata. Return
values are checked and the computed hash is wiped.

The format has no key ID, key epoch, domain-separation field, or hardware-backed
counter. Ed25519 therefore remains bound to one embedded key and one manifest
format. No self-developed cryptography is permitted.

Host tests cover valid images, both slots, minimum/maximum payloads, bad keys
and signatures, tampered payloads/manifests, truncation, flags, reserved words,
vector/address boundaries, target-slot mismatch, protected ranges, and
rollback versions. The verifier also executes the RFC 8032 Ed25519 empty
message vector and the standard SHA-512 empty-message digest.

Known limits are full-call-graph stack analysis, external cryptographic review,
and a systematic fuzz campaign. These are not claimed as complete here.
