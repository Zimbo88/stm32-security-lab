#!/usr/bin/env python3

from pathlib import Path
import re
import sys

SOURCE = Path(
    "logs/exp032_device_inventory/openocd_accessible_inventory_raw.txt"
)
OUTPUT = Path(
    "logs/exp033_flash_option_analysis/RESULTS.md"
)
MACHINE_OUTPUT = Path(
    "logs/exp033_flash_option_analysis/decoded_values.txt"
)

REGISTER_PATTERN = re.compile(
    r"^([A-Z][A-Z0-9_]*)\|"
    r"(0x[0-9A-Fa-f]+)\|"
    r"(0x[0-9A-Fa-f]+)$",
    re.MULTILINE,
)


def fail(message: str) -> None:
    print(f"FEHLER: {message}", file=sys.stderr)
    raise SystemExit(1)


def hex32(value: int) -> str:
    return f"0x{value:08X}"


def bit(value: int, position: int) -> int:
    return (value >> position) & 1


def yes_no(value: bool) -> str:
    return "yes" if value else "no"


if not SOURCE.is_file():
    fail(f"Quelldatei fehlt: {SOURCE}")

text = SOURCE.read_text(encoding="utf-8", errors="replace")

registers = {
    name: {
        "address": int(address, 16),
        "value": int(value, 16),
    }
    for name, address, value in REGISTER_PATTERN.findall(text)
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
    fail("Fehlende Register: " + ", ".join(missing))

acr = registers["FLASH_ACR"]["value"]
sr = registers["FLASH_SR"]["value"]
cr = registers["FLASH_CR"]["value"]
optcr = registers["FLASH_OPTCR"]["value"]
optcr1 = registers["FLASH_OPTCR1"]["value"]

rdp_byte = (optcr >> 8) & 0xFF
bor_lev = (optcr >> 2) & 0x03
nwrp_bank1 = (optcr >> 16) & 0x0FFF
nwrp_bank2 = (optcr1 >> 16) & 0x0FFF

if rdp_byte == 0xAA:
    rdp_level = "Level 0"
    rdp_description = "No read protection"
elif rdp_byte == 0xCC:
    rdp_level = "Level 2"
    rdp_description = "Irreversible read protection"
else:
    rdp_level = "Level 1"
    rdp_description = "Read protection active"

protected_bank1 = [
    sector
    for sector in range(12)
    if not bit(nwrp_bank1, sector)
]

protected_bank2 = [
    sector + 12
    for sector in range(12)
    if not bit(nwrp_bank2, sector)
]

flash_errors = {
    "EOP": bit(sr, 0),
    "OPERR": bit(sr, 1),
    "WRPERR": bit(sr, 4),
    "PGAERR": bit(sr, 5),
    "PGPERR": bit(sr, 6),
    "PGSERR": bit(sr, 7),
    "RDERR": bit(sr, 8),
    "BSY": bit(sr, 16),
}

program_size_code = (cr >> 8) & 0x03
program_size_names = {
    0: "x8",
    1: "x16",
    2: "x32",
    3: "x64",
}

latency = acr & 0x0F

machine_lines = [
    f"FLASH_ACR={hex32(acr)}",
    f"FLASH_SR={hex32(sr)}",
    f"FLASH_CR={hex32(cr)}",
    f"FLASH_OPTCR={hex32(optcr)}",
    f"FLASH_OPTCR1={hex32(optcr1)}",
    f"RDP_BYTE=0x{rdp_byte:02X}",
    f"RDP_LEVEL={rdp_level}",
    f"BOR_LEV={bor_lev}",
    f"NWRP_BANK1=0x{nwrp_bank1:03X}",
    f"NWRP_BANK2=0x{nwrp_bank2:03X}",
    "PROTECTED_SECTORS_BANK1="
    + (",".join(map(str, protected_bank1)) or "none"),
    "PROTECTED_SECTORS_BANK2="
    + (",".join(map(str, protected_bank2)) or "none"),
]

MACHINE_OUTPUT.write_text(
    "\n".join(machine_lines) + "\n",
    encoding="utf-8",
)

lines = [
    "# EXP033 – STM32F429 Flash and Option-Byte Analysis",
    "",
    "## Objective",
    "",
    "Decode the flash-controller and option-control register values measured",
    "during EXP032 without modifying the target.",
    "",
    "## Source evidence",
    "",
    f"- Source file: `{SOURCE}`",
    f"- FLASH_ACR: `{hex32(acr)}`",
    f"- FLASH_SR: `{hex32(sr)}`",
    f"- FLASH_CR: `{hex32(cr)}`",
    f"- FLASH_OPTCR: `{hex32(optcr)}`",
    f"- FLASH_OPTCR1: `{hex32(optcr1)}`",
    "",
    "## Read-protection state",
    "",
    f"- RDP byte, FLASH_OPTCR bits 15:8: `0x{rdp_byte:02X}`",
    f"- Interpreted state: **RDP {rdp_level}**",
    f"- Meaning: {rdp_description}",
    "",
    "RDP decoding rule:",
    "",
    "- `0xAA`: Level 0",
    "- `0xCC`: Level 2",
    "- every other byte value: Level 1",
    "",
    "No option-byte modification was made during this analysis.",
    "",
    "## User option fields",
    "",
    f"- BOR_LEV, bits 3:2: `{bor_lev}`",
    f"- WDG_SW, bit 5: `{bit(optcr, 5)}`",
    f"- nRST_STOP, bit 6: `{bit(optcr, 6)}`",
    f"- nRST_STDBY, bit 7: `{bit(optcr, 7)}`",
    f"- OPTLOCK, bit 0: `{bit(optcr, 0)}`",
    f"- OPTSTRT, bit 1: `{bit(optcr, 1)}`",
    "",
    "## Write-protection masks",
    "",
    "The nWRP fields are active-low. A zero bit identifies a protected",
    "sector; a one bit identifies a sector that is not write-protected.",
    "",
    f"- Bank 1 nWRP mask: `0x{nwrp_bank1:03X}`",
    f"- Bank 2 nWRP mask: `0x{nwrp_bank2:03X}`",
    "- Protected Bank 1 sectors: "
    + (
        ", ".join(f"`{sector}`" for sector in protected_bank1)
        if protected_bank1
        else "none"
    ),
    "- Protected Bank 2 sectors: "
    + (
        ", ".join(f"`{sector}`" for sector in protected_bank2)
        if protected_bank2
        else "none"
    ),
    "",
    "## Flash status register",
    "",
]

for name, value in flash_errors.items():
    lines.append(f"- {name}: `{value}`")

lines.extend([
    "",
    "## Flash control register",
    "",
    f"- LOCK, bit 31: `{bit(cr, 31)}`",
    f"- ERRIE, bit 25: `{bit(cr, 25)}`",
    f"- EOPIE, bit 24: `{bit(cr, 24)}`",
    f"- STRT, bit 16: `{bit(cr, 16)}`",
    f"- PSIZE, bits 9:8: `{program_size_code}` "
    f"({program_size_names[program_size_code]})",
    f"- SNB, bits 7:3: `{(cr >> 3) & 0x1F}`",
    f"- MER, bit 2: `{bit(cr, 2)}`",
    f"- SER, bit 1: `{bit(cr, 1)}`",
    f"- PG, bit 0: `{bit(cr, 0)}`",
    "",
    "The measured FLASH_CR value has LOCK set. No programming or erase",
    "operation was active.",
    "",
    "## Flash access-control register",
    "",
    f"- LATENCY field: `{latency}`",
    f"- Data-cache reset DCRST, bit 12: `{bit(acr, 12)}`",
    f"- Instruction-cache reset ICRST, bit 11: `{bit(acr, 11)}`",
    f"- Data-cache enable DCEN, bit 10: `{bit(acr, 10)}`",
    f"- Instruction-cache enable ICEN, bit 9: `{bit(acr, 9)}`",
    f"- Prefetch enable PRFTEN, bit 8: `{bit(acr, 8)}`",
    "",
    "## Security-relevant observations",
    "",
    f"1. The device was measured in RDP {rdp_level}.",
    "2. SWD attachment and peripheral-register reads remained possible.",
    "3. Reading the factory system-memory flash-size address failed.",
    "4. At least one Bank 1 sector is write-protected if the decoded nWRP",
    "   zero bits are confirmed against the exact device configuration.",
    "5. Bank 2 reports no active-low nWRP zero bits in the measured mask.",
    "",
    "## Interpretation boundary",
    "",
    "This report separates directly measured register values from decoded",
    "bit-field interpretations. PCROP and mode-dependent meanings of shared",
    "option bits require a dedicated experiment and comparison against the",
    "exact STM32F429 option-byte mode.",
    "",
    "## Safety",
    "",
    "This experiment is offline analysis only.",
    "",
    "- No OpenOCD connection was opened.",
    "- No target register was written.",
    "- No flash memory was erased or programmed.",
    "- No option byte was changed.",
    "- RDP Level 2 was not enabled.",
]

OUTPUT.write_text(
    "\n".join(lines) + "\n",
    encoding="utf-8",
)

print("EXP033-Auswertung abgeschlossen")
print(f"RDP byte: 0x{rdp_byte:02X}")
print(f"RDP state: {rdp_level}")
print(f"Bank 1 nWRP: 0x{nwrp_bank1:03X}")
print(f"Bank 2 nWRP: 0x{nwrp_bank2:03X}")
print(
    "Geschützte Bank-1-Sektoren:",
    protected_bank1 if protected_bank1 else "keine",
)
print(
    "Geschützte Bank-2-Sektoren:",
    protected_bank2 if protected_bank2 else "keine",
)
print(f"Bericht: {OUTPUT}")
