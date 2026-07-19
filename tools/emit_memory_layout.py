#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path

from stm32f429_layout import LAYOUT, LAYOUT_PROFILES, PROFILE_IDS, ROOT

HEADER_PATH = ROOT / "firmware" / "common" / "stm32f429_memory_layout.h"
LD_PATH = ROOT / "firmware" / "common" / "stm32f429_memory_layout.ld"
MK_PATH = ROOT / "firmware" / "common" / "stm32f429_memory_layout.mk"


def _hex(value: int) -> str:
    return f"0x{value:08X}"


def _c_bool(value: bool) -> str:
    return "1U" if value else "0U"


def _macro_name(prefix: str, name: str) -> str:
    return f"{prefix}_{name.upper()}"


def _render_sector_header(l: dict[str, object]) -> str:
    lines = [
        f"#define STM32F429_FLASH_SECTOR_COUNT {len(l['flash_sectors'])}UL",
        "",
    ]
    for sector in l["flash_sectors"]:
        sid = sector["id"]
        lines.extend(
            [
                f"#define STM32F429_FLASH_SECTOR_{sid}_BANK {sector['bank']}UL",
                f"#define STM32F429_FLASH_SECTOR_{sid}_BASE {_hex(sector['base'])}UL",
                f"#define STM32F429_FLASH_SECTOR_{sid}_SIZE {_hex(sector['size'])}UL",
                f"#define STM32F429_FLASH_SECTOR_{sid}_END {_hex(sector['end'])}UL",
                "",
            ]
        )
    return "\n".join(lines)


def _render_sector_ld(l: dict[str, object]) -> str:
    lines = [f"STM32F429_FLASH_SECTOR_COUNT = {len(l['flash_sectors'])};", ""]
    for sector in l["flash_sectors"]:
        sid = sector["id"]
        lines.extend(
            [
                f"STM32F429_FLASH_SECTOR_{sid}_BANK = {sector['bank']};",
                f"STM32F429_FLASH_SECTOR_{sid}_BASE = {_hex(sector['base'])};",
                f"STM32F429_FLASH_SECTOR_{sid}_SIZE = {_hex(sector['size'])};",
                f"STM32F429_FLASH_SECTOR_{sid}_END = {_hex(sector['end'])};",
                "",
            ]
        )
    return "\n".join(lines)


def _region_header(prefix: str, l: dict[str, object]) -> str:
    keys = ("BASE", "SIZE", "END", "FIRST_SECTOR", "LAST_SECTOR")
    values = (
        l[f"{prefix.lower()}_base"],
        l[f"{prefix.lower()}_size"],
        l[f"{prefix.lower()}_end"],
        l[f"{prefix.lower()}_first_sector"],
        l[f"{prefix.lower()}_last_sector"],
    )
    lines = []
    for key, value in zip(keys, values, strict=True):
        suffix = "UL"
        text = _hex(value) if key not in ("FIRST_SECTOR", "LAST_SECTOR") else str(value)
        lines.append(f"#define {_macro_name('STM32F429', prefix)}_{key} {text}{suffix}")
    return "\n".join(lines)


def _region_ld(prefix: str, l: dict[str, object]) -> str:
    keys = ("BASE", "SIZE", "END", "FIRST_SECTOR", "LAST_SECTOR")
    values = (
        l[f"{prefix.lower()}_base"],
        l[f"{prefix.lower()}_size"],
        l[f"{prefix.lower()}_end"],
        l[f"{prefix.lower()}_first_sector"],
        l[f"{prefix.lower()}_last_sector"],
    )
    lines = []
    for key, value in zip(keys, values, strict=True):
        text = _hex(value) if key not in ("FIRST_SECTOR", "LAST_SECTOR") else str(value)
        lines.append(f"{_macro_name('STM32F429', prefix)}_{key} = {text};")
    return "\n".join(lines)


def _slot_header(name: str, l: dict[str, object]) -> str:
    prefix = f"STM32F429_SLOT_{name.upper()}"
    lower = f"slot_{name.lower()}"
    fields = (
        ("ID", l[lower]["id"]),
        ("SIGNED_IMAGE_BASE", l[f"{lower}_signed_image_base"]),
        ("MANIFEST_BASE", l[f"{lower}_manifest_base"]),
        ("SIGNATURE_BASE", l[f"{lower}_signature_base"]),
        ("PAYLOAD_BASE", l[f"{lower}_payload_base"]),
        ("END", l[f"{lower}_end"]),
        ("SIZE", l[f"{lower}_size"]),
        ("PAYLOAD_MAX_SIZE", l[f"{lower}_payload_max_size"]),
        ("FIRST_SECTOR", l[f"{lower}_first_sector"]),
        ("LAST_SECTOR", l[f"{lower}_last_sector"]),
    )
    lines = []
    for field, value in fields:
        text = str(value) if field in ("ID", "FIRST_SECTOR", "LAST_SECTOR") else _hex(value)
        lines.append(f"#define {prefix}_{field} {text}UL")
    return "\n".join(lines)


def _slot_ld(name: str, l: dict[str, object]) -> str:
    prefix = f"STM32F429_SLOT_{name.upper()}"
    lower = f"slot_{name.lower()}"
    fields = (
        ("ID", l[lower]["id"]),
        ("SIGNED_IMAGE_BASE", l[f"{lower}_signed_image_base"]),
        ("MANIFEST_BASE", l[f"{lower}_manifest_base"]),
        ("SIGNATURE_BASE", l[f"{lower}_signature_base"]),
        ("PAYLOAD_BASE", l[f"{lower}_payload_base"]),
        ("END", l[f"{lower}_end"]),
        ("SIZE", l[f"{lower}_size"]),
        ("PAYLOAD_MAX_SIZE", l[f"{lower}_payload_max_size"]),
        ("FIRST_SECTOR", l[f"{lower}_first_sector"]),
        ("LAST_SECTOR", l[f"{lower}_last_sector"]),
    )
    lines = []
    for field, value in fields:
        text = str(value) if field in ("ID", "FIRST_SECTOR", "LAST_SECTOR") else _hex(value)
        lines.append(f"{prefix}_{field} = {text};")
    return "\n".join(lines)


def _profile_macro(profile: str) -> str:
    return profile.upper().replace("-", "_")


def render_header(layout: dict[str, object] | None = None) -> str:
    l = LAYOUT if layout is None else layout
    profile_defines = "\n".join(
        f"#define STM32F429_LAYOUT_PROFILE_{_profile_macro(profile)} {profile_id}UL"
        for profile, profile_id in sorted(PROFILE_IDS.items())
    )
    return f"""/* Generated from config/stm32f429_memory_layout.json profile {l["profile"]}. */
#ifndef STM32F429_MEMORY_LAYOUT_H
#define STM32F429_MEMORY_LAYOUT_H

{profile_defines}
#define STM32F429_LAYOUT_PROFILE_ID {l["layout_profile_id"]}UL
#define STM32F429_LAYOUT_PROFILE_NAME "{l["profile"]}"
#define STM32F429_MCU_NAME "{l["mcu"]}"

#define STM32F429_FLASH_BASE {_hex(l["flash_base"])}UL
#define STM32F429_FLASH_TOTAL_SIZE {_hex(l["flash_total_size"])}UL
#define STM32F429_FLASH_SIZE_KIB {l["flash_total_size"] // 1024}UL
#define STM32F429_FLASH_END {_hex(l["flash_end"])}UL
#define STM32F429_FLASH_BANK1_BASE {_hex(l["flash_bank1_base"])}UL
#define STM32F429_FLASH_BANK1_SIZE {_hex(l["flash_bank1_size"])}UL
#define STM32F429_FLASH_BANK1_END {_hex(l["flash_bank1_end"])}UL
#define STM32F429_FLASH_BANK2_BASE {_hex(l["flash_bank2_base"])}UL
#define STM32F429_FLASH_BANK2_SIZE {_hex(l["flash_bank2_size"])}UL
#define STM32F429_FLASH_BANK2_END {_hex(l["flash_bank2_end"])}UL

{_render_sector_header(l)}

#define STM32F429_BOOTLOADER_BASE {_hex(l["bootloader_base"])}UL
#define STM32F429_BOOTLOADER_SIZE {_hex(l["bootloader_size"])}UL
#define STM32F429_BOOTLOADER_END {_hex(l["bootloader_end"])}UL
#define STM32F429_BOOTLOADER_FIRST_SECTOR {l["bootloader_first_sector"]}UL
#define STM32F429_BOOTLOADER_LAST_SECTOR {l["bootloader_last_sector"]}UL

#define STM32F429_BOOT_METADATA_FORMAT_VERSION {l["boot_metadata_format_version"]}UL
#define STM32F429_BOOT_METADATA_RECORD_SIZE {_hex(l["boot_metadata_record_size"])}UL

{_region_header("BOOT_METADATA_A", l)}

{_region_header("BOOT_METADATA_B", l)}

{_region_header("UPDATE_METADATA", l)}

{_slot_header("A", l)}

{_slot_header("B", l)}

{_region_header("RECOVERY", l)}

#define STM32F429_SIGNED_IMAGE_BASE {_hex(l["signed_image_base"])}UL
#define STM32F429_SIGNED_MANIFEST_SIZE {_hex(l["signed_manifest_size"])}UL
#define STM32F429_SIGNED_SIGNATURE_SIZE {_hex(l["signed_signature_size"])}UL
#define STM32F429_SIGNED_IMAGE_HEADER_SIZE {_hex(l["signed_image_header_size"])}UL
#define STM32F429_SIGNATURE_BASE {_hex(l["signature_base"])}UL

#define STM32F429_APPLICATION_BASE {_hex(l["application_base"])}UL
#define STM32F429_APPLICATION_FLASH_END {_hex(l["application_flash_end"])}UL
#define STM32F429_APPLICATION_PAYLOAD_MAX_SIZE {_hex(l["application_payload_max_size"])}UL
#define STM32F429_APPLICATION_MIN_PAYLOAD_SIZE {_hex(l["application_min_payload_size"])}UL

#define STM32F429_SIGNED_IMAGE_FLAGS_ALLOWED_MASK {_hex(l["signed_image_flags_allowed_mask"])}UL

#define STM32F429_MAIN_SRAM_BASE {_hex(l["main_sram_base"])}UL
#define STM32F429_MAIN_SRAM_SUPPORTED_SIZE {_hex(l["main_sram_supported_size"])}UL
#define STM32F429_MAIN_SRAM_SUPPORTED_END {_hex(l["main_sram_supported_end"])}UL
#define STM32F429_MAIN_SRAM_DEVICE_SIZE {_hex(l["main_sram_device_size"])}UL
#define STM32F429_MAIN_SRAM_DEVICE_END {_hex(l["main_sram_device_end"])}UL

#define STM32F429_APPLICATION_MSP_BASE {_hex(l["application_msp_base"])}UL
#define STM32F429_APPLICATION_MSP_END {_hex(l["application_msp_end"])}UL
#define STM32F429_APPLICATION_MSP_ALIGNMENT {_hex(l["application_msp_alignment"])}UL

#define STM32F429_CCM_BASE {_hex(l["ccm_base"])}UL
#define STM32F429_CCM_SIZE {_hex(l["ccm_size"])}UL
#define STM32F429_CCM_END {_hex(l["ccm_end"])}UL
#define STM32F429_CCM_APPLICATION_SUPPORTED {_c_bool(l["ccm_application_supported"])}
#define STM32F429_SRAM_EXECUTION_SUPPORTED {_c_bool(l["sram_execution_supported"])}

#endif
"""


def render_ld(layout: dict[str, object] | None = None) -> str:
    l = LAYOUT if layout is None else layout
    return f"""/* Generated from config/stm32f429_memory_layout.json profile {l["profile"]}. */
STM32F429_LAYOUT_PROFILE_ID = {l["layout_profile_id"]};
STM32F429_LAYOUT_PROFILE_STM32F429_1M = {PROFILE_IDS["stm32f429_1m"]};
STM32F429_LAYOUT_PROFILE_STM32F429_2M = {PROFILE_IDS["stm32f429_2m"]};

STM32F429_FLASH_BASE = {_hex(l["flash_base"])};
STM32F429_FLASH_TOTAL_SIZE = {_hex(l["flash_total_size"])};
STM32F429_FLASH_SIZE_KIB = {l["flash_total_size"] // 1024};
STM32F429_FLASH_END = {_hex(l["flash_end"])};
STM32F429_FLASH_BANK1_BASE = {_hex(l["flash_bank1_base"])};
STM32F429_FLASH_BANK1_SIZE = {_hex(l["flash_bank1_size"])};
STM32F429_FLASH_BANK1_END = {_hex(l["flash_bank1_end"])};
STM32F429_FLASH_BANK2_BASE = {_hex(l["flash_bank2_base"])};
STM32F429_FLASH_BANK2_SIZE = {_hex(l["flash_bank2_size"])};
STM32F429_FLASH_BANK2_END = {_hex(l["flash_bank2_end"])};

{_render_sector_ld(l)}

STM32F429_BOOTLOADER_BASE = {_hex(l["bootloader_base"])};
STM32F429_BOOTLOADER_SIZE = {_hex(l["bootloader_size"])};
STM32F429_BOOTLOADER_END = {_hex(l["bootloader_end"])};
STM32F429_BOOTLOADER_FIRST_SECTOR = {l["bootloader_first_sector"]};
STM32F429_BOOTLOADER_LAST_SECTOR = {l["bootloader_last_sector"]};

STM32F429_BOOT_METADATA_FORMAT_VERSION = {l["boot_metadata_format_version"]};
STM32F429_BOOT_METADATA_RECORD_SIZE = {_hex(l["boot_metadata_record_size"])};

{_region_ld("BOOT_METADATA_A", l)}

{_region_ld("BOOT_METADATA_B", l)}

{_region_ld("UPDATE_METADATA", l)}

{_slot_ld("A", l)}

{_slot_ld("B", l)}

{_region_ld("RECOVERY", l)}

STM32F429_SIGNED_IMAGE_BASE = {_hex(l["signed_image_base"])};
STM32F429_SIGNED_MANIFEST_SIZE = {_hex(l["signed_manifest_size"])};
STM32F429_SIGNED_SIGNATURE_SIZE = {_hex(l["signed_signature_size"])};
STM32F429_SIGNED_IMAGE_HEADER_SIZE = {_hex(l["signed_image_header_size"])};
STM32F429_SIGNATURE_BASE = {_hex(l["signature_base"])};

STM32F429_APPLICATION_BASE = {_hex(l["application_base"])};
STM32F429_APPLICATION_FLASH_END = {_hex(l["application_flash_end"])};
STM32F429_APPLICATION_PAYLOAD_MAX_SIZE = {_hex(l["application_payload_max_size"])};
STM32F429_APPLICATION_MIN_PAYLOAD_SIZE = {_hex(l["application_min_payload_size"])};

STM32F429_MAIN_SRAM_BASE = {_hex(l["main_sram_base"])};
STM32F429_MAIN_SRAM_SUPPORTED_SIZE = {_hex(l["main_sram_supported_size"])};
STM32F429_MAIN_SRAM_SUPPORTED_END = {_hex(l["main_sram_supported_end"])};
"""


def render_make(layout: dict[str, object] | None = None) -> str:
    l = LAYOUT if layout is None else layout
    return f"""# Generated from config/stm32f429_memory_layout.json profile {l["profile"]}.
STM32F429_LAYOUT_PROFILE := {l["profile"]}
STM32F429_LAYOUT_PROFILE_ID := {l["layout_profile_id"]}
STM32F429_MCU := {l["mcu"]}
STM32F429_FLASH_BASE_HEX := {l["flash_base"]:08x}
STM32F429_FLASH_END_HEX := {l["flash_end"]:08x}
STM32F429_FLASH_TOTAL_SIZE_BYTES := {l["flash_total_size"]}
STM32F429_FLASH_SIZE_KIB := {l["flash_total_size"] // 1024}
STM32F429_BOOTLOADER_BASE_HEX := {l["bootloader_base"]:08x}
STM32F429_BOOTLOADER_SIZE_BYTES := {l["bootloader_size"]}
STM32F429_BOOT_METADATA_A_BASE_HEX := {l["boot_metadata_a_base"]:08x}
STM32F429_BOOT_METADATA_A_SIZE_BYTES := {l["boot_metadata_a_size"]}
STM32F429_BOOT_METADATA_A_FIRST_SECTOR := {l["boot_metadata_a_first_sector"]}
STM32F429_BOOT_METADATA_A_LAST_SECTOR := {l["boot_metadata_a_last_sector"]}
STM32F429_BOOT_METADATA_B_BASE_HEX := {l["boot_metadata_b_base"]:08x}
STM32F429_BOOT_METADATA_B_SIZE_BYTES := {l["boot_metadata_b_size"]}
STM32F429_BOOT_METADATA_B_FIRST_SECTOR := {l["boot_metadata_b_first_sector"]}
STM32F429_BOOT_METADATA_B_LAST_SECTOR := {l["boot_metadata_b_last_sector"]}
STM32F429_UPDATE_METADATA_BASE_HEX := {l["update_metadata_base"]:08x}
STM32F429_UPDATE_METADATA_SIZE_BYTES := {l["update_metadata_size"]}
STM32F429_UPDATE_METADATA_FIRST_SECTOR := {l["update_metadata_first_sector"]}
STM32F429_UPDATE_METADATA_LAST_SECTOR := {l["update_metadata_last_sector"]}
STM32F429_APPLICATION_BASE_HEX := {l["application_base"]:08x}
STM32F429_APPLICATION_PAYLOAD_MAX_SIZE_BYTES := {l["application_payload_max_size"]}
STM32F429_SLOT_A_SIGNED_IMAGE_BASE_HEX := {l["slot_a_signed_image_base"]:08x}
STM32F429_SLOT_A_APPLICATION_BASE_HEX := {l["slot_a_payload_base"]:08x}
STM32F429_SLOT_A_END_HEX := {l["slot_a_end"]:08x}
STM32F429_SLOT_A_PAYLOAD_MAX_SIZE_BYTES := {l["slot_a_payload_max_size"]}
STM32F429_SLOT_A_FIRST_SECTOR := {l["slot_a_first_sector"]}
STM32F429_SLOT_A_LAST_SECTOR := {l["slot_a_last_sector"]}
STM32F429_SLOT_B_SIGNED_IMAGE_BASE_HEX := {l["slot_b_signed_image_base"]:08x}
STM32F429_SLOT_B_APPLICATION_BASE_HEX := {l["slot_b_payload_base"]:08x}
STM32F429_SLOT_B_END_HEX := {l["slot_b_end"]:08x}
STM32F429_SLOT_B_PAYLOAD_MAX_SIZE_BYTES := {l["slot_b_payload_max_size"]}
STM32F429_SLOT_B_FIRST_SECTOR := {l["slot_b_first_sector"]}
STM32F429_SLOT_B_LAST_SECTOR := {l["slot_b_last_sector"]}
STM32F429_RECOVERY_BASE_HEX := {l["recovery_base"]:08x}
STM32F429_RECOVERY_SIZE_BYTES := {l["recovery_size"]}
STM32F429_RECOVERY_FIRST_SECTOR := {l["recovery_first_sector"]}
STM32F429_RECOVERY_LAST_SECTOR := {l["recovery_last_sector"]}
STM32F429_SIGNED_MANIFEST_SIZE_BYTES := {l["signed_manifest_size"]}
STM32F429_SIGNED_SIGNATURE_SIZE_BYTES := {l["signed_signature_size"]}
STM32F429_SIGNED_IMAGE_HEADER_SIZE_BYTES := {l["signed_image_header_size"]}
STM32F429_BOOT_METADATA_RECORD_SIZE_BYTES := {l["boot_metadata_record_size"]}
"""


def _check(path: Path, expected: str) -> bool:
    return path.exists() and path.read_text(encoding="ascii") == expected


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    parser.add_argument(
        "--profile",
        choices=sorted(LAYOUT_PROFILES),
        default=LAYOUT["profile"],
        help="layout profile to render; the repository generated files track the default profile",
    )
    args = parser.parse_args()
    layout = LAYOUT_PROFILES[args.profile]

    outputs = (
        (HEADER_PATH, render_header(layout)),
        (LD_PATH, render_ld(layout)),
        (MK_PATH, render_make(layout)),
    )
    if args.check:
        stale = [str(path) for path, expected in outputs if not _check(path, expected)]
        if stale:
            for path in stale:
                print(f"stale generated memory layout: {path}")
            return 1
        return 0

    for path, text in outputs:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="ascii")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
