# EXP066 CLI

The prompt is `rp> `. Input is bounded to 79 characters; CR, LF, and CRLF are
accepted. Commands are a static allowlist. There is no address argument or
generic memory/register access.

Commands: `help`, `version`, `device info`, `device uid`, `device flash-size`,
`device option-bytes`, `reset cause`, `clock show`, all required `registers`
groups, `memory regions`, `log show`, `log clear`, `fault show`, `fault clear`,
`test list`, `test run gpio|button|clock|ram`, `security status`, and
`module list`.

Only the exact `test run gpio`, `test run button`, `test run clock`, and
`test run ram` commands return PASS. Other `test ...` strings are rejected.
The `registers nvic`, `registers systick`, `registers mpu`, `registers flash`,
`registers pwr`, and `registers syscfg` commands return an explicit
unavailable message in EXP066 instead of reading broader register windows.

EXP067 host tools:

- `tools/module_pack.py` packs and signs a module payload.
- `tools/module_verify.py` verifies a package against a 32-byte public key
  supplied as raw bytes or hex.
- `tools/module_inspect.py` prints deterministic package metadata and can
  verify the signature when a public key is supplied.

EXP068 host tools:

- `tools/bytecode_asm.py` assembles bytecode source.
- `tools/bytecode_vm.py` runs raw bytecode with explicit capabilities or a
  signed bytecode package. Native packages and ELF files are rejected.
