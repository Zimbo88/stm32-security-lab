#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path

from stm32f429_layout import LAYOUT, ROOT


HEADER_PATH = ROOT / "firmware" / "common" / "stm32f429_memory_layout.h"
LD_PATH = ROOT / "firmware" / "common" / "stm32f429_memory_layout.ld"
MK_PATH = ROOT / "firmware" / "common" / "stm32f429_memory_layout.mk"


def _hex(value: int) -> str:
    return f"0x{value:08X}"


def _c_bool(value: bool) -> str:
    return "1U" if value else "0U"


def render_header() -> str:
    l = LAYOUT
    return f"""/* Generated from config/stm32f429_memory_layout.json. */
#ifndef STM32F429_MEMORY_LAYOUT_H
#define STM32F429_MEMORY_LAYOUT_H

#define STM32F429_FLASH_BASE {_hex(l["flash_base"])}UL
#define STM32F429_FLASH_TOTAL_SIZE {_hex(l["flash_total_size"])}UL
#define STM32F429_FLASH_END {_hex(l["flash_end"])}UL
#define STM32F429_FLASH_BANK1_BASE {_hex(l["flash_bank1_base"])}UL
#define STM32F429_FLASH_BANK1_SIZE {_hex(l["flash_bank1_size"])}UL
#define STM32F429_FLASH_BANK1_END {_hex(l["flash_bank1_end"])}UL

#define STM32F429_BOOTLOADER_BASE {_hex(l["bootloader_base"])}UL
#define STM32F429_BOOTLOADER_SIZE {_hex(l["bootloader_size"])}UL
#define STM32F429_BOOTLOADER_END {_hex(l["bootloader_end"])}UL

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


def render_ld() -> str:
    l = LAYOUT
    return f"""/* Generated from config/stm32f429_memory_layout.json. */
STM32F429_FLASH_BASE = {_hex(l["flash_base"])};
STM32F429_FLASH_TOTAL_SIZE = {_hex(l["flash_total_size"])};
STM32F429_FLASH_END = {_hex(l["flash_end"])};
STM32F429_FLASH_BANK1_BASE = {_hex(l["flash_bank1_base"])};
STM32F429_FLASH_BANK1_SIZE = {_hex(l["flash_bank1_size"])};
STM32F429_FLASH_BANK1_END = {_hex(l["flash_bank1_end"])};

STM32F429_BOOTLOADER_BASE = {_hex(l["bootloader_base"])};
STM32F429_BOOTLOADER_SIZE = {_hex(l["bootloader_size"])};
STM32F429_BOOTLOADER_END = {_hex(l["bootloader_end"])};

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


def render_make() -> str:
    l = LAYOUT
    return f"""# Generated from config/stm32f429_memory_layout.json.
STM32F429_BOOTLOADER_SIZE_BYTES := {l["bootloader_size"]}
STM32F429_APPLICATION_BASE_HEX := {l["application_base"]:08x}
STM32F429_APPLICATION_PAYLOAD_MAX_SIZE_BYTES := {l["application_payload_max_size"]}
"""


def _check(path: Path, expected: str) -> bool:
    return path.exists() and path.read_text(encoding="ascii") == expected


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()

    outputs = (
        (HEADER_PATH, render_header()),
        (LD_PATH, render_ld()),
        (MK_PATH, render_make()),
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
