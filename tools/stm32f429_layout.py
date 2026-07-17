from __future__ import annotations

import json
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
CONFIG_PATH = ROOT / "config" / "stm32f429_memory_layout.json"


def _int(value: Any) -> int:
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        return int(value, 0)
    raise TypeError(f"unsupported integer value {value!r}")


def load_layout(path: Path = CONFIG_PATH) -> dict[str, Any]:
    raw = json.loads(path.read_text(encoding="ascii"))

    flash_base = _int(raw["flash"]["base"])
    flash_total_size = _int(raw["flash"]["total_size"])
    flash_bank1_size = _int(raw["flash"]["bank1_size"])

    bootloader_base = _int(raw["bootloader"]["base"])
    bootloader_size = _int(raw["bootloader"]["size"])
    signed_image_base = bootloader_base + bootloader_size

    manifest_size = _int(raw["signed_image"]["manifest_size"])
    signature_size = _int(raw["signed_image"]["signature_size"])
    header_size = _int(raw["signed_image"]["header_size"])
    signature_base = signed_image_base + manifest_size
    application_base = signed_image_base + header_size

    flash_bank1_end = flash_base + flash_bank1_size
    flash_end = flash_base + flash_total_size
    application_flash_end = flash_bank1_end

    main_sram_base = _int(raw["main_sram"]["base"])
    main_sram_supported_size = _int(raw["main_sram"]["supported_size"])
    main_sram_device_size = _int(raw["main_sram"]["device_size"])
    main_sram_supported_end = main_sram_base + main_sram_supported_size
    main_sram_device_end = main_sram_base + main_sram_device_size

    ccm_base = _int(raw["ccm"]["base"])
    ccm_size = _int(raw["ccm"]["size"])
    ccm_end = ccm_base + ccm_size

    layout = {
        "target": raw["target"],
        "flash_base": flash_base,
        "flash_total_size": flash_total_size,
        "flash_end": flash_end,
        "flash_bank1_base": flash_base,
        "flash_bank1_size": flash_bank1_size,
        "flash_bank1_end": flash_bank1_end,
        "bootloader_base": bootloader_base,
        "bootloader_size": bootloader_size,
        "bootloader_end": signed_image_base,
        "signed_image_base": signed_image_base,
        "signed_manifest_size": manifest_size,
        "signed_signature_size": signature_size,
        "signed_image_header_size": header_size,
        "signature_base": signature_base,
        "application_base": application_base,
        "application_flash_end": application_flash_end,
        "application_payload_max_size": application_flash_end - application_base,
        "application_min_payload_size": _int(raw["policy"]["minimum_payload_size"]),
        "signed_image_flags_allowed_mask": _int(raw["policy"]["manifest_flags_allowed_mask"]),
        "application_msp_alignment": _int(raw["policy"]["msp_alignment"]),
        "main_sram_base": main_sram_base,
        "main_sram_supported_size": main_sram_supported_size,
        "main_sram_supported_end": main_sram_supported_end,
        "main_sram_device_size": main_sram_device_size,
        "main_sram_device_end": main_sram_device_end,
        "application_msp_base": main_sram_base,
        "application_msp_end": main_sram_supported_end,
        "ccm_base": ccm_base,
        "ccm_size": ccm_size,
        "ccm_end": ccm_end,
        "ccm_application_supported": bool(raw["ccm"]["application_supported"]),
        "sram_execution_supported": False,
    }
    validate_layout(layout)
    return layout


def validate_layout(layout: dict[str, Any]) -> None:
    if layout["flash_base"] != layout["bootloader_base"]:
        raise ValueError("bootloader must start at flash base")
    if layout["bootloader_end"] != layout["signed_image_base"]:
        raise ValueError("signed image must start after bootloader")
    if layout["signed_manifest_size"] != 0x60:
        raise ValueError("manifest size must remain 96 bytes")
    if layout["signed_signature_size"] != 0x40:
        raise ValueError("signature size must remain 64 bytes")
    if layout["signed_image_header_size"] < (
        layout["signed_manifest_size"] + layout["signed_signature_size"]
    ):
        raise ValueError("signed header overlaps payload")
    if layout["application_base"] != (
        layout["signed_image_base"] + layout["signed_image_header_size"]
    ):
        raise ValueError("application base must follow signed-image header")
    if layout["application_flash_end"] > layout["flash_bank1_end"]:
        raise ValueError("application region must remain in flash bank 1")
    if layout["application_payload_max_size"] <= layout["application_min_payload_size"]:
        raise ValueError("application payload region is too small")
    if layout["application_msp_base"] != layout["main_sram_base"]:
        raise ValueError("application MSP must use supported main SRAM")
    if layout["application_msp_end"] != layout["main_sram_supported_end"]:
        raise ValueError("application MSP must end at supported SRAM boundary")


LAYOUT = load_layout()
