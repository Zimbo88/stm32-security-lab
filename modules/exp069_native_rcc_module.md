# EXP069 native RCC analysis module

This is the first harmless native module definition for EXP069.

The signed package is produced in tests with:

- package type: `TYPE_NATIVE`,
- native format: `NMV1`,
- API version: `1`,
- simulation ID: `1` (`SIM_RCC_ANALYSIS`),
- capabilities:
  - `0x00006801` / `RCC_READ`,
  - `0x00006802` / `OUTPUT`.

The payload contains opaque position-independent Thumb stub bytes:

```text
00 b5 00 bd
```

The host-side simulator does not execute those bytes. It uses the validated
`SIM_RCC_ANALYSIS` metadata to emulate a module that reads only the curated RCC
snapshot indices and emits:

- channel 1: raw `RCC_CR`,
- channel 2: `RCC_CR.HSIRDY`,
- channel 3: `RCC_CR.PLLON`,
- channel 4: `RCC_CFGR.SWS`,
- channel 5: raw `RCC_CSR`.

No arbitrary memory or peripheral addresses are accepted by this module.
