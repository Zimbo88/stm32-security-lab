# Verified OpenOCD RDP commands

## Enable RDP Level 1

    openocd \
      -f interface/stlink.cfg \
      -f target/stm32f4x.cfg \
      -c "init" \
      -c "reset halt" \
      -c "flash probe 0" \
      -c "stm32f2x lock 0" \
      -c "shutdown"

Expected effect:
- RDP changes from Level 0 to Level 1.
- User Flash contents remain present.
- External debug/read access becomes restricted after option-byte reload.
- A power cycle is required before testing the resulting protection state.

## Return to RDP Level 0

    openocd \
      -f interface/stlink.cfg \
      -f target/stm32f4x.cfg \
      -c "init" \
      -c "reset halt" \
      -c "flash probe 0" \
      -c "stm32f2x unlock 0" \
      -c "shutdown"

Expected destructive effect:
- RDP returns from Level 1 to Level 0.
- Internal user Flash is mass-erased.
- Bootloader and application must be restored from EXP024.
- Sector-0 WRP must be checked and reapplied if necessary.

Forbidden:
- Never write RDP value 0xCC.
- Never use raw OPTCR writes for this experiment.
