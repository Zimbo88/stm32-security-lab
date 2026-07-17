# Module threat model

EXP067 treats package bytes as hostile. Validation is fail-closed for integer
ranges, overlap, truncation, hashes, signatures, signer identity, versions,
capabilities, and reserved fields. No package code is executed in EXP067.

EXP070 treats persistent catalog and slot contents as hostile after reset.
Recovery accepts only complete CRC-valid records, ignores malformed tails, and
keeps the newest confirmed module separate from unconfirmed candidates. The
installer never accepts raw Flash addresses and does not expose Flash-write,
option-byte, RDP, arbitrary memory, or arbitrary execution services to modules.
