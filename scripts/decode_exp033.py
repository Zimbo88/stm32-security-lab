#!/usr/bin/env python3

import re
import sys
from pathlib import Path

source = Path(
    "logs/exp032_device_inventory/"
    "openocd_accessible_inventory_raw.txt"
)
output_dir = Path("logs/exp033_flash_option_analysis")
values_file = output_dir / "decoded_values.txt"
report_file = output_dir / "RESULTS.md"

if not source.is_file():
    print(f"ERROR: Source file is missing: {source}", file=sys.stderr)
    sys.exit(1)

text = source.read_text(encoding="utf-8", errors="replace")

pattern = re.compile(
    r"^([A-Z][A-Z0-9_]*)\|"
    r"(0x[0-9A-Fa-f]+)\|"
    r"(0x[0-9A-Fa-f]+)$",
    re.MULTILINE,
)

registers = {
    name: int(value, 16)
    for name, address, value in pattern.findall(text)
}

required = [
    "FLASH_ACR",
    "FLASH_SR",
    "FLASH_CR",
    "FLASH_OPTCR",
    "FLASH_OPTCR1",
]

missing = [name for name in required if name not in registers]

if missing:
    print(
        "ERROR: Registers are missing: " + ", ".join(missing),
        file=sys.stderr,
    )
    sys.exit(1)

acr = registers["FLASH_ACR"]
sr = registers["FLASH_SR"]
cr = registers["FLASH_CR"]
optcr = registers["FLASH_OPTCR"]
optcr1 = registers["FLASH_OPTCR1"]

rdp_byte = (optcr >> 8) & 0xFF
nwrp_bank1 = (optcr >> 16) & 0xFFF
nwrp_bank2 = (optcr1 >> 16) & 0xFFF

if rdp_byte == 0xAA:
    rdp_level = "Level 0"
elif rdp_byte == 0xCC:
    rdp_level = "Level 2"
else:
    rdp_level = "Level 1"

protected_bank1 = [
    str(sector)
    for sector in range(12)
    if ((nwrp_bank1 >> sector) & 1) == 0
]

protected_bank2 = [
    str(sector + 12)
    for sector in range(12)
    if ((nwrp_bank2 >> sector) & 1) == 0
]

bank1_text = ",".join(protected_bank1) or "none"
bank2_text = ",".join(protected_bank2) or "none"

output_dir.mkdir(parents=True, exist_ok=True)

values = [
    f"FLASH_ACR=0x{acr:08X}",
    f"FLASH_SR=0x{sr:08X}",
    f"FLASH_CR=0x{cr:08X}",
    f"FLASH_OPTCR=0x{optcr:08X}",
    f"FLASH_OPTCR1=0x{optcr1:08X}",
    f"RDP_BYTE=0x{rdp_byte:02X}",
    f"RDP_LEVEL={rdp_level}",
    f"NWRP_BANK1=0x{nwrp_bank1:03X}",
    f"NWRP_BANK2=0x{nwrp_bank2:03X}",
    f"PROTECTED_SECTORS_BANK1={bank1_text}",
    f"PROTECTED_SECTORS_BANK2={bank2_text}",
]

values_file.write_text(
    "\n".join(values) + "\n",
    encoding="utf-8",
)

report = f"""# EXP033 – Flash and Option-Byte Analysis

## Measured values

- FLASH_ACR: `0x{acr:08X}`
- FLASH_SR: `0x{sr:08X}`
- FLASH_CR: `0x{cr:08X}`
- FLASH_OPTCR: `0x{optcr:08X}`
- FLASH_OPTCR1: `0x{optcr1:08X}`

## Protection state

- RDP byte: `0x{rdp_byte:02X}`
- Interpreted RDP state: **{rdp_level}**
- Bank 1 nWRP mask: `0x{nwrp_bank1:03X}`
- Bank 2 nWRP mask: `0x{nwrp_bank2:03X}`
- Protected Bank 1 sectors: `{bank1_text}`
- Protected Bank 2 sectors: `{bank2_text}`

## Flash controller

- Controller locked: `{bool(cr & (1 << 31))}`
- Busy flag: `{bool(sr & (1 << 16))}`
- Programming active: `{bool(cr & 1)}`
- Sector erase active: `{bool(cr & (1 << 1))}`
- Mass erase active: `{bool(cr & (1 << 2))}`

## Safety

This report was generated from previously recorded values.

No target connection, flash write, erase or option-byte change was performed.
"""

report_file.write_text(report, encoding="utf-8")

print("EXP033 erfolgreich ausgewertet.")
print(f"RDP: {rdp_level}, Byte 0x{rdp_byte:02X}")
print(f"Bank 1 geschützt: {bank1_text}")
print(f"Bank 2 geschützt: {bank2_text}")
print(f"Bericht: {report_file}")
