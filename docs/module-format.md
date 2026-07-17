# EXP067 module format

Packages use little-endian fields and consist of a 240-byte fixed header,
capability table, and payload. `HEADER_FORMAT` in `tools/module_format.py` is
authoritative (`struct.calcsize` is asserted as 240). The header contains 17
uint32 metadata fields, a 64-byte SHA-512 payload digest, 16-byte key ID,
CRC32, 64-byte Ed25519 signature, and 16 reserved zero bytes.

The payload hash covers exactly the payload range. CRC32 is calculated over a
header with CRC and signature zeroed. The Ed25519 signature covers the
normalized header with the CRC included and the signature zeroed, followed by
the capability table and payload.

Capabilities are nonzero uint32 little-endian IDs. The parser accepts at most
64 capability entries. A platform may pass a fixed supported-capability set to
reject package capabilities outside that set. Key ID is SHA-256 of the public
key truncated to 16 bytes.

Parsers reject malformed ranges, overlap, gaps between the capability table and
payload, trailing bytes after the payload, duplicate capabilities, unsupported
types, nonzero flags, bad versions, rollback versions, CRCs, hashes,
signatures, and reserved bytes.

EXP068 defines the first executable payload class for `TYPE_BYTECODE` packages.
The payload itself uses the `BCV1` bytecode header documented in
`docs/exp068-bytecode-vm.md`. `TYPE_NATIVE` remains rejected by the EXP068 VM.

EXP069 defines the constrained `TYPE_NATIVE` payload class. Native payloads use
the `NMV1` header documented in `docs/exp069-native-modules.md`; they are not
ELF files and have no dynamic linker or symbol resolver.
