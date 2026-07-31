# Stage-0 protection boundary

| Region | Address | Sectors | UART installer |
|---|---:|---:|---|
| Stage 0/public key | `0x08000000..0x08008000` | 0-1 | never writable |
| Metadata A/B | `0x08008000..0x08010000` | 2-3 | journal commit only |
| Reserved update metadata | `0x08010000..0x08020000` | 4 | never writable |
| Slot A | `0x08020000..0x08080000` | 5-7 | candidate only |
| Slot B | `0x08080000..0x080E0000` | 8-10 | candidate only |
| Recovery reserve | `0x080E0000..0x08100000` | 11 | never writable |

The installer constructs a restricted `boot_flash_t` containing only metadata
A, metadata B, and the selected inactive slot. It checks ranges with checked
addition, sector mapping, alignment, and readback. Header and bounds checks
complete before candidate erase. Recovery bootstrap writes only a signed
Slot-A candidate; it does not write Stage 0.

Host tests attempt Stage-0, public-key, metadata, update-metadata, and recovery
ranges and verify rejection or unchanged storage. The MPU maps the first 128
KiB read-only/non-executable to application code. The MPU is not a substitute
for WRP because privileged code can disable it.

Status: software boundary `IMPLEMENTED` and `HOST TESTED`; physical WRP
protection `DOCUMENTED ONLY` and `NOT IMPLEMENTED`.
