# EXP068 bytecode VM

EXP068 introduces a bytecode-only sandbox for signed modules. It does not
execute native modules and does not load ELF files.

The bytecode payload begins with a 16-byte `BCV1` header followed by fixed
4-byte instructions. The VM validates the header before execution: version,
header size, code size, entry offset, reserved fields, instruction alignment,
opcode set, shift bounds, syscall argument count, and stack limit.

Execution is bounded by:

- an instruction budget supplied by the caller,
- a per-program stack limit from the bytecode header,
- the VM maximum stack limit of 64 uint32 values,
- deterministic uint32 arithmetic,
- fail-closed stack underflow, stack overflow, invalid branch, invalid syscall,
  invalid bytecode, and budget-exhaustion checks.

The syscall interface is curated. Bytecode never supplies an address. `RCC_READ`
accepts only fixed register indices for `RCC_CR`, `RCC_CFGR`, and `RCC_CSR`,
using values supplied by the host snapshot. `EMIT` appends `(channel, value)`
pairs to the VM result. Both syscalls require signed package capabilities when
running from an EXP067 package.

Capability IDs:

- `0x00006801`: `RCC_READ`
- `0x00006802`: `OUTPUT`

The first bytecode module is `modules/exp068_rcc_analysis.bcasm`. It emits raw
`RCC_CR`, `RCC_CR.HSIRDY`, `RCC_CR.PLLON`, `RCC_CFGR.SWS`, and raw `RCC_CSR`
from the curated snapshot. It does not read arbitrary memory or alter clocks.

Host tooling:

- `tools/bytecode_asm.py` assembles `.bcasm` source to raw bytecode.
- `tools/bytecode_vm.py` executes raw bytecode with explicit capabilities or a
  signed EXP067 package and public key.

Example:

```sh
python3 tools/bytecode_asm.py modules/exp068_rcc_analysis.bcasm \
  --output /tmp/exp068_rcc.bc --print-caps
python3 tools/bytecode_vm.py /tmp/exp068_rcc.bc \
  --cap 0x00006801 --cap 0x00006802 \
  --rcc-cr 0x00000003 --rcc-cfgr 0x00000004 --rcc-csr 0x08000000
```

Native modules and ELF loading are explicitly out of scope for EXP068.
