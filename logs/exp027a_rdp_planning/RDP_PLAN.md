# EXP027A – RDP Level 1 Planning

## Current state

- MCU: STM32F429
- RDP: Level 0
- SWD: available
- Bootloader sector 0: write protected
- Application sector 2: writable
- Secure boot: operational
- Installed image version: 2
- Minimum accepted image version: 2

## Intended RDP1 behavior

After enabling RDP Level 1:

- External Flash readback through SWD must fail.
- Normal debug access to protected Flash is restricted.
- Boot from internal user Flash must continue.
- Secure bootloader must still validate and start the signed application.
- Sector 0 write protection should remain configured.

## Recovery consequence

Returning from RDP Level 1 to RDP Level 0 causes a complete internal Flash
mass erase.

Recovery therefore requires:

1. Restore bootloader at 0x08000000.
2. Restore signed version-2 image at 0x08008000.
3. Reapply sector-0 write protection.
4. Verify secure boot over UART.
5. Recheck all option bytes.

## Forbidden actions

- Do not select RDP Level 2.
- Do not run mass erase during EXP027A.
- Do not write raw OPTCR values without decoding every affected bit.
- Do not modify RDP before confirming the exact recovery command.
- Do not proceed with unstable power or USB connections.

## Go/no-go conditions for EXP027B

Proceed only if all are true:

- Full Flash backup hashes pass.
- Recovery archive hash passes.
- Current option registers are documented.
- Sector 0 WRP is confirmed.
- Signed application boots successfully.
- Exact command for setting RDP1 is independently verified.
- Exact recovery procedure for returning to RDP0 is documented.
