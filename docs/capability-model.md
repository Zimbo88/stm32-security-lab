# Capability model

Capabilities are signed uint32 IDs in the package table. They cannot be added
at runtime. EXP067 records and verifies them but does not execute modules.

EXP068 uses capabilities to gate bytecode syscalls:

| ID | Name | Scope |
|---:|---|---|
| `0x00006801` | `RCC_READ` | Read one of the curated RCC snapshot indices: `RCC_CR`, `RCC_CFGR`, `RCC_CSR`. |
| `0x00006802` | `OUTPUT` | Emit deterministic `(channel, value)` analysis results. |

No capability permits native execution, arbitrary memory access, arbitrary
register access, Flash writes, option-byte changes, or RDP changes.
