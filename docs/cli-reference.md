# EXP066 CLI

The prompt is `rp> `. Input is bounded to 79 characters; CR, LF, and CRLF are
accepted. Commands are a static allowlist. There is no address argument or
generic memory/register access.

Commands: `help`, `version`, `boot status`, `device info`, `device uid`,
`device flash-size`, `device option-bytes`, `reset cause`, `clock show`, all
required `registers` groups, `memory regions`, `log show`, `log clear`,
`fault show`, `fault clear`, `health status`, `health acknowledge`,
`led status`, `led test healthy|degraded|update|recovery|fault|security`,
`led test stop`, `easteregg knightrider`, `easteregg retro`, `easteregg stop`,
`test list`, `test run gpio|button|clock|ram`, `security status`, and
`module list`.

Only the exact `test run gpio`, `test run button`, `test run clock`, and
`test run ram` commands return PASS. Other `test ...` strings are rejected.
The `registers nvic`, `registers systick`, `registers mpu`, and
`registers flash` commands now print curated read-only snapshots. The
`registers pwr` and `registers syscfg` commands return an explicit unavailable
message in EXP066 instead of changing peripheral clocks.

EXP071 health and Easter egg commands:

- `health status` prints the active health state, automatic state, temporary
  override state, LED mask, boot reset cause, retained fault-record status, and
  mandatory self-test status.
- `health acknowledge` clears a degraded historical reset/fault indication for
  the current boot. It does not erase Flash or change option bytes.
- `led test healthy`, `led test degraded`, `led test update`,
  `led test recovery`, `led test fault`, and `led test security` start bounded
  non-blocking LED tests and then restore automatic health indication.
- `led test stop` stops the temporary LED test.
- `easteregg knightrider` starts the bounded fast Knight-Rider LED pattern.
- `easteregg retro` reports audio unavailable unless external audio hardware is
  explicitly added in a future revision.
- `easteregg stop` stops temporary LED/audio activity.

How to trigger the Easter egg at the `rp> ` prompt:

```text
easteregg knightrider
```

Stop it with:

```text
easteregg stop
```

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

EXP069 host tool:

- `tools/native_loader.py` validates and simulates a signed `TYPE_NATIVE`
  package. It rejects bytecode packages, malformed native payloads, unknown
  capabilities, wrong signer identity, incompatible ABI/platform versions, and
  rollback versions.

EXP070 host tool:

- `tools/module_install.py` models atomic A/B installation against a
  JSON-backed simulated Flash image. It supports `list`, `inspect`, `install`,
  `verify`, `activate`, `confirm`, `rollback`, `quarantine`,
  `remove-candidate`, `revoke-signer`, and `catalog-recovery`. Commands operate
  on module IDs and predefined slots only; they do not accept raw Flash
  addresses.
