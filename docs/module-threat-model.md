# Module threat model

EXP067 treats package bytes as hostile. Validation is fail-closed for integer
ranges, overlap, truncation, hashes, signatures, signer identity, versions,
capabilities, and reserved fields. No package code is executed in EXP067.
