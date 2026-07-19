from __future__ import annotations

import json
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
CONFIG_PATH = ROOT / "config" / "stm32f429_memory_layout.json"
UINT32_MAX = 0xFFFFFFFF
PROFILE_IDS = {
    "stm32f429_1m": 1,
    "stm32f429_2m": 2,
}


def _int(value: Any) -> int:
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        return int(value, 0)
    raise TypeError(f"unsupported integer value {value!r}")


def _checked_add(left: int, right: int, name: str) -> int:
    if left < 0 or right < 0 or left > UINT32_MAX or right > UINT32_MAX:
        raise ValueError(f"{name} operands must fit uint32")
    if right > UINT32_MAX - left:
        raise ValueError(f"{name} overflows uint32")
    return left + right


def _read_region(raw: dict[str, Any], name: str) -> dict[str, int]:
    base = _int(raw["base"])
    size = _int(raw["size"])
    return {
        "name": name,
        "base": base,
        "size": size,
        "end": _checked_add(base, size, f"{name} end"),
        "first_sector": _int(raw["first_sector"]),
        "last_sector": _int(raw["last_sector"]),
    }


def _sector_end(sector: dict[str, int]) -> int:
    return _checked_add(sector["base"], sector["size"], f"sector {sector['id']} end")


def _validate_sector_region(
    name: str,
    region: dict[str, int],
    sectors_by_id: dict[int, dict[str, int]],
) -> None:
    first = sectors_by_id.get(region["first_sector"])
    last = sectors_by_id.get(region["last_sector"])
    if first is None or last is None:
        raise ValueError(f"{name} references an unknown sector")
    if region["last_sector"] < region["first_sector"]:
        raise ValueError(f"{name} sector order is invalid")
    if region["base"] != first["base"]:
        raise ValueError(f"{name} must start at the first sector boundary")
    if region["end"] != _sector_end(last):
        raise ValueError(f"{name} must end at the last sector boundary")
    expected = region["base"]
    for sector_id in range(region["first_sector"], region["last_sector"] + 1):
        sector = sectors_by_id.get(sector_id)
        if sector is None:
            raise ValueError(f"{name} has a missing sector")
        if sector["base"] != expected:
            raise ValueError(f"{name} sector coverage is not contiguous")
        expected = _sector_end(sector)
    if expected != region["end"]:
        raise ValueError(f"{name} sector coverage does not match region end")


def _slot_from_region(
    name: str,
    raw_slot: dict[str, Any],
    header_size: int,
    manifest_size: int,
    sectors_by_id: dict[int, dict[str, int]],
) -> dict[str, int]:
    region = _read_region(raw_slot, f"slot_{name}")
    _validate_sector_region(f"slot {name}", region, sectors_by_id)
    signed_image_base = region["base"]
    signature_base = _checked_add(
        signed_image_base,
        manifest_size,
        f"slot {name} signature base",
    )
    payload_base = _checked_add(
        signed_image_base,
        header_size,
        f"slot {name} payload base",
    )
    if payload_base >= region["end"]:
        raise ValueError(f"slot {name} header leaves no payload space")
    return {
        "id": _int(raw_slot["id"]),
        "signed_image_base": signed_image_base,
        "manifest_base": signed_image_base,
        "signature_base": signature_base,
        "payload_base": payload_base,
        "end": region["end"],
        "size": region["size"],
        "payload_max_size": region["end"] - payload_base,
        "first_sector": region["first_sector"],
        "last_sector": region["last_sector"],
    }


def _select_profile(raw: dict[str, Any], profile: str | None) -> tuple[str, dict[str, Any]]:
    if "profiles" not in raw:
        if profile is not None:
            raise ValueError("profile selection requires a profiled layout config")
        return "legacy-single", raw

    profiles = raw["profiles"]
    if not isinstance(profiles, dict) or not profiles:
        raise ValueError("layout config must define profiles")

    selected_profile = profile or str(raw.get("default_profile", ""))
    if selected_profile not in profiles:
        raise ValueError(f"unknown STM32F429 layout profile: {selected_profile}")

    if selected_profile not in PROFILE_IDS:
        raise ValueError(f"layout profile lacks a stable numeric ID: {selected_profile}")

    selected = dict(profiles[selected_profile])
    selected["profile"] = selected_profile
    selected["default_profile"] = raw.get("default_profile")
    return selected_profile, selected


def load_layout(path: Path = CONFIG_PATH, profile: str | None = None) -> dict[str, Any]:
    raw = json.loads(path.read_text(encoding="ascii"))
    profile_name, raw = _select_profile(raw, profile)

    flash_base = _int(raw["flash"]["base"])
    flash_total_size = _int(raw["flash"]["total_size"])
    flash_end = _checked_add(flash_base, flash_total_size, "flash end")

    banks = []
    for bank_raw in raw["flash"]["banks"]:
        base = _int(bank_raw["base"])
        size = _int(bank_raw["size"])
        banks.append(
            {
                "id": _int(bank_raw["id"]),
                "base": base,
                "size": size,
                "end": _checked_add(base, size, f"bank {bank_raw['id']} end"),
                "first_sector": _int(bank_raw["first_sector"]),
                "last_sector": _int(bank_raw["last_sector"]),
            }
        )
    banks.sort(key=lambda bank: bank["id"])

    sectors = []
    for sector_raw in raw["flash"]["sectors"]:
        base = _int(sector_raw["base"])
        size = _int(sector_raw["size"])
        sectors.append(
            {
                "id": _int(sector_raw["id"]),
                "bank": _int(sector_raw["bank"]),
                "base": base,
                "size": size,
                "end": _checked_add(base, size, f"sector {sector_raw['id']} end"),
            }
        )
    sectors.sort(key=lambda sector: sector["id"])
    sectors_by_id = {sector["id"]: sector for sector in sectors}

    manifest_size = _int(raw["signed_image"]["manifest_size"])
    signature_size = _int(raw["signed_image"]["signature_size"])
    header_size = _int(raw["signed_image"]["header_size"])

    bootloader = _read_region(raw["bootloader"], "bootloader")
    metadata_a = _read_region(raw["boot_metadata"]["copy_a"], "boot_metadata_a")
    metadata_b = _read_region(raw["boot_metadata"]["copy_b"], "boot_metadata_b")
    update_metadata = _read_region(raw["update_metadata"], "update_metadata")
    recovery = _read_region(raw["recovery"], "recovery")
    slot_a = _slot_from_region(
        "a",
        raw["slots"]["a"],
        header_size,
        manifest_size,
        sectors_by_id,
    )
    slot_b = _slot_from_region(
        "b",
        raw["slots"]["b"],
        header_size,
        manifest_size,
        sectors_by_id,
    )

    main_sram_base = _int(raw["main_sram"]["base"])
    main_sram_supported_size = _int(raw["main_sram"]["supported_size"])
    main_sram_device_size = _int(raw["main_sram"]["device_size"])
    main_sram_supported_end = _checked_add(
        main_sram_base,
        main_sram_supported_size,
        "supported SRAM end",
    )
    main_sram_device_end = _checked_add(
        main_sram_base,
        main_sram_device_size,
        "device SRAM end",
    )

    ccm_base = _int(raw["ccm"]["base"])
    ccm_size = _int(raw["ccm"]["size"])
    ccm_end = _checked_add(ccm_base, ccm_size, "CCM end")

    if len(banks) == 1:
        bank2 = {
            "id": 2,
            "base": flash_end,
            "size": 0,
            "end": flash_end,
            "first_sector": len(sectors),
            "last_sector": len(sectors) - 1,
        }
    elif len(banks) >= 2:
        bank2 = banks[1]
    else:
        raise ValueError("STM32F429 flash must have at least one bank")

    layout = {
        "profile": profile_name,
        "layout_profile": profile_name,
        "layout_profile_id": PROFILE_IDS.get(profile_name, 0),
        "default_profile": raw.get("default_profile"),
        "target": raw["target"],
        "mcu": raw.get("mcu", raw["target"]),
        "profile_description": raw.get("description", ""),
        "flash_base": flash_base,
        "flash_total_size": flash_total_size,
        "flash_end": flash_end,
        "flash_banks": banks,
        "flash_sectors": sectors,
        "flash_bank1_base": banks[0]["base"],
        "flash_bank1_size": banks[0]["size"],
        "flash_bank1_end": banks[0]["end"],
        "flash_bank2_base": bank2["base"],
        "flash_bank2_size": bank2["size"],
        "flash_bank2_end": bank2["end"],
        "bootloader_base": bootloader["base"],
        "bootloader_size": bootloader["size"],
        "bootloader_end": bootloader["end"],
        "bootloader_first_sector": bootloader["first_sector"],
        "bootloader_last_sector": bootloader["last_sector"],
        "boot_metadata_format_version": _int(raw["boot_metadata"]["format_version"]),
        "boot_metadata_record_size": _int(raw["boot_metadata"]["record_size"]),
        "boot_metadata_a_base": metadata_a["base"],
        "boot_metadata_a_size": metadata_a["size"],
        "boot_metadata_a_end": metadata_a["end"],
        "boot_metadata_a_first_sector": metadata_a["first_sector"],
        "boot_metadata_a_last_sector": metadata_a["last_sector"],
        "boot_metadata_b_base": metadata_b["base"],
        "boot_metadata_b_size": metadata_b["size"],
        "boot_metadata_b_end": metadata_b["end"],
        "boot_metadata_b_first_sector": metadata_b["first_sector"],
        "boot_metadata_b_last_sector": metadata_b["last_sector"],
        "update_metadata_base": update_metadata["base"],
        "update_metadata_size": update_metadata["size"],
        "update_metadata_end": update_metadata["end"],
        "update_metadata_first_sector": update_metadata["first_sector"],
        "update_metadata_last_sector": update_metadata["last_sector"],
        "slot_a": slot_a,
        "slot_b": slot_b,
        "slot_a_signed_image_base": slot_a["signed_image_base"],
        "slot_a_manifest_base": slot_a["manifest_base"],
        "slot_a_signature_base": slot_a["signature_base"],
        "slot_a_payload_base": slot_a["payload_base"],
        "slot_a_end": slot_a["end"],
        "slot_a_size": slot_a["size"],
        "slot_a_payload_max_size": slot_a["payload_max_size"],
        "slot_a_first_sector": slot_a["first_sector"],
        "slot_a_last_sector": slot_a["last_sector"],
        "slot_b_signed_image_base": slot_b["signed_image_base"],
        "slot_b_manifest_base": slot_b["manifest_base"],
        "slot_b_signature_base": slot_b["signature_base"],
        "slot_b_payload_base": slot_b["payload_base"],
        "slot_b_end": slot_b["end"],
        "slot_b_size": slot_b["size"],
        "slot_b_payload_max_size": slot_b["payload_max_size"],
        "slot_b_first_sector": slot_b["first_sector"],
        "slot_b_last_sector": slot_b["last_sector"],
        "recovery_base": recovery["base"],
        "recovery_size": recovery["size"],
        "recovery_end": recovery["end"],
        "recovery_first_sector": recovery["first_sector"],
        "recovery_last_sector": recovery["last_sector"],
        "signed_manifest_size": manifest_size,
        "signed_signature_size": signature_size,
        "signed_image_header_size": header_size,
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

    # Legacy single-image aliases intentionally point at Slot A until Stage-0
    # slot selection is introduced.
    layout["signed_image_base"] = layout["slot_a_signed_image_base"]
    layout["signature_base"] = layout["slot_a_signature_base"]
    layout["application_base"] = layout["slot_a_payload_base"]
    layout["application_flash_end"] = layout["slot_a_end"]
    layout["application_payload_max_size"] = layout["slot_a_payload_max_size"]

    validate_layout(layout)
    return layout


def _expect_range(
    name: str,
    layout: dict[str, Any],
    base: int,
    end: int,
    first_sector: int,
    last_sector: int,
) -> None:
    if layout[f"{name}_base"] != base or layout[f"{name}_end"] != end:
        raise ValueError(f"{name} address range changed")
    if (
        layout[f"{name}_first_sector"] != first_sector
        or layout[f"{name}_last_sector"] != last_sector
    ):
        raise ValueError(f"{name} sector assignment changed")


def validate_layout(layout: dict[str, Any]) -> None:
    sectors = layout["flash_sectors"]
    banks = layout["flash_banks"]
    sectors_by_id = {sector["id"]: sector for sector in sectors}

    if layout["target"] != "STM32F429IGT6":
        raise ValueError("unexpected STM32 target")
    if layout["mcu"] != "STM32F429IGT6":
        raise ValueError("unexpected STM32 MCU")
    if layout["profile"] not in PROFILE_IDS:
        raise ValueError("unknown STM32F429 layout profile")
    if layout["layout_profile_id"] != PROFILE_IDS[layout["profile"]]:
        raise ValueError("layout profile ID changed")
    if layout["flash_base"] != 0x08000000:
        raise ValueError("STM32F429 flash must start at 0x08000000")
    if len(banks) not in (1, 2) or banks[0]["id"] != 1:
        raise ValueError("STM32F429 flash must have one or two configured banks")
    if banks[0]["base"] != layout["flash_base"]:
        raise ValueError("bank 1 must start at flash base")
    if banks[0]["size"] != 0x00100000:
        raise ValueError("STM32F429 bank 1 must be 1 MiB")
    if len(banks) == 1:
        if banks[0]["end"] != layout["flash_end"]:
            raise ValueError("single-bank profile must end at physical flash end")
    else:
        if banks[1]["id"] != 2:
            raise ValueError("second STM32F429 bank must have bank id 2")
        if banks[1]["size"] != 0x00100000:
            raise ValueError("legacy STM32F429 bank 2 must be 1 MiB")
        if banks[0]["end"] != banks[1]["base"]:
            raise ValueError("bank 2 must start exactly at the bank 1 boundary")
        if banks[1]["end"] != layout["flash_end"]:
            raise ValueError("bank 2 must end at physical flash end")

    if layout["profile"] == "stm32f429_1m":
        if layout["flash_total_size"] != 0x00100000 or layout["flash_end"] != 0x08100000:
            raise ValueError("STM32F429IGT6 hardware profile must be 1 MiB")
        if len(banks) != 1:
            raise ValueError("STM32F429IGT6 hardware profile must not define bank 2")
        if len(sectors) != 12 or [sector["id"] for sector in sectors] != list(range(12)):
            raise ValueError("STM32F429IGT6 hardware profile must enumerate sectors 0 through 11")
    elif layout["profile"] == "stm32f429_2m":
        if layout["flash_total_size"] != 0x00200000 or layout["flash_end"] != 0x08200000:
            raise ValueError("legacy STM32F429 reference profile must remain 2 MiB")
        if len(banks) != 2:
            raise ValueError("legacy STM32F429 reference profile must retain two banks")
        if len(sectors) != 24 or [sector["id"] for sector in sectors] != list(range(24)):
            raise ValueError("legacy STM32F429 reference profile must enumerate sectors 0 through 23")
    else:
        raise ValueError("unsupported STM32F429 layout profile")

    expected_sector_sizes = [0x4000] * 4 + [0x10000] + [0x20000] * 7
    if layout["profile"] == "stm32f429_2m":
        expected_sector_sizes += [0x4000] * 4 + [0x10000] + [0x20000] * 7
    expected = layout["flash_base"]
    for sector, size in zip(sectors, expected_sector_sizes, strict=True):
        if sector["base"] != expected or sector["size"] != size:
            raise ValueError(f"sector {sector['id']} does not match STM32F429 geometry")
        expected = sector["end"]
    if expected != layout["flash_end"]:
        raise ValueError("sector map does not end at physical flash end")

    for bank in banks:
        first = sectors_by_id[bank["first_sector"]]
        last = sectors_by_id[bank["last_sector"]]
        if first["base"] != bank["base"] or last["end"] != bank["end"]:
            raise ValueError(f"bank {bank['id']} sector coverage is invalid")
        for sector_id in range(bank["first_sector"], bank["last_sector"] + 1):
            if sectors_by_id[sector_id]["bank"] != bank["id"]:
                raise ValueError(f"sector {sector_id} has the wrong bank id")

    if layout["signed_manifest_size"] != 0x60:
        raise ValueError("manifest size must remain 96 bytes")
    if layout["signed_signature_size"] != 0x40:
        raise ValueError("signature size must remain 64 bytes")
    if layout["signed_image_header_size"] != 0x200:
        raise ValueError("signed-image header size must remain 512 bytes")
    if layout["signed_image_header_size"] < (
        layout["signed_manifest_size"] + layout["signed_signature_size"]
    ):
        raise ValueError("signed header overlaps payload")

    _expect_range("bootloader", layout, 0x08000000, 0x08008000, 0, 1)
    _expect_range("boot_metadata_a", layout, 0x08008000, 0x0800C000, 2, 2)
    _expect_range("boot_metadata_b", layout, 0x0800C000, 0x08010000, 3, 3)
    _expect_range("update_metadata", layout, 0x08010000, 0x08020000, 4, 4)
    if layout["profile"] == "stm32f429_1m":
        _expect_range("recovery", layout, 0x080E0000, 0x08100000, 11, 11)
    else:
        _expect_range("recovery", layout, 0x081E0000, 0x08200000, 23, 23)

    if layout["bootloader_size"] != 0x8000:
        raise ValueError("Stage 0 must remain exactly 32 KiB")
    if layout["boot_metadata_a_first_sector"] == layout["boot_metadata_b_first_sector"]:
        raise ValueError("metadata copies must occupy separate sectors")

    for slot_name in ("slot_a", "slot_b"):
        slot = layout[slot_name]
        if slot["manifest_base"] != slot["signed_image_base"]:
            raise ValueError(f"{slot_name} manifest must start at signed-image base")
        if slot["signature_base"] != slot["signed_image_base"] + layout["signed_manifest_size"]:
            raise ValueError(f"{slot_name} signature must follow manifest")
        if slot["payload_base"] != slot["signed_image_base"] + layout["signed_image_header_size"]:
            raise ValueError(f"{slot_name} payload must follow signed-image header")
        if slot["payload_max_size"] != slot["end"] - slot["payload_base"]:
            raise ValueError(f"{slot_name} payload max size is inconsistent")
        if slot["payload_max_size"] > slot["size"] - layout["signed_image_header_size"]:
            raise ValueError(f"{slot_name} payload exceeds slot capacity")
        if (slot["payload_base"] & 0xFF) != 0:
            raise ValueError(f"{slot_name} payload base must be VTOR-aligned")
        if slot["signed_image_base"] < layout["flash_base"] or slot["end"] > layout["flash_end"]:
            raise ValueError(f"{slot_name} is outside physical flash")

    if layout["slot_a"]["id"] != 0 or layout["slot_b"]["id"] != 1:
        raise ValueError("slot IDs must be stable")
    if layout["slot_a_size"] != layout["slot_b_size"]:
        raise ValueError("Slot A and Slot B capacities must be equal")
    if layout["slot_a_payload_max_size"] != layout["slot_b_payload_max_size"]:
        raise ValueError("Slot A and Slot B payload capacities must be equal")
    if layout["slot_a_signed_image_base"] != 0x08020000:
        raise ValueError("Slot A base changed")
    if layout["profile"] == "stm32f429_1m":
        if layout["slot_a_signed_image_base"] != 0x08020000:
            raise ValueError("Slot A base changed")
        if layout["slot_b_signed_image_base"] != 0x08080000:
            raise ValueError("Slot B base changed")
        if layout["slot_a_end"] != 0x08080000:
            raise ValueError("Slot A end changed")
        if layout["slot_b_end"] != 0x080E0000:
            raise ValueError("Slot B end changed")
        if layout["slot_a_first_sector"] != 5 or layout["slot_a_last_sector"] != 7:
            raise ValueError("Slot A sector assignment changed")
        if layout["slot_b_first_sector"] != 8 or layout["slot_b_last_sector"] != 10:
            raise ValueError("Slot B sector assignment changed")
        if max(sector["id"] for sector in sectors) > 11:
            raise ValueError("1 MiB profile cannot contain sectors above 11")
    elif layout["slot_b_signed_image_base"] != 0x08100000:
        raise ValueError("Slot B base changed")
    if layout["slot_a_payload_base"] != 0x08020200:
        raise ValueError("Slot A vector address changed")
    expected_slot_b_payload = (
        0x08080200 if layout["profile"] == "stm32f429_1m" else 0x08100200
    )
    if layout["slot_b_payload_base"] != expected_slot_b_payload:
        raise ValueError("Slot B vector address changed")

    named_ranges = [
        ("bootloader", layout["bootloader_base"], layout["bootloader_end"]),
        ("boot_metadata_a", layout["boot_metadata_a_base"], layout["boot_metadata_a_end"]),
        ("boot_metadata_b", layout["boot_metadata_b_base"], layout["boot_metadata_b_end"]),
        ("update_metadata", layout["update_metadata_base"], layout["update_metadata_end"]),
        ("slot_a", layout["slot_a_signed_image_base"], layout["slot_a_end"]),
        ("slot_b", layout["slot_b_signed_image_base"], layout["slot_b_end"]),
        ("recovery", layout["recovery_base"], layout["recovery_end"]),
    ]
    expected_base = layout["flash_base"]
    for name, base, end in sorted(named_ranges, key=lambda item: item[1]):
        if base != expected_base:
            raise ValueError(f"unexpected gap or overlap before {name}")
        if end <= base:
            raise ValueError(f"{name} has an invalid range")
        expected_base = end
    if expected_base != layout["flash_end"]:
        raise ValueError("named regions must cover the complete physical flash")

    if layout["application_base"] != layout["slot_a_payload_base"]:
        raise ValueError("legacy application base must alias Slot A")
    if layout["application_flash_end"] != layout["slot_a_end"]:
        raise ValueError("legacy application end must alias Slot A")
    if layout["signed_image_base"] != layout["slot_a_signed_image_base"]:
        raise ValueError("legacy signed-image base must alias Slot A")
    if layout["signature_base"] != layout["slot_a_signature_base"]:
        raise ValueError("legacy signature base must alias Slot A")
    if layout["application_payload_max_size"] <= layout["application_min_payload_size"]:
        raise ValueError("application payload region is too small")
    if layout["application_msp_base"] != layout["main_sram_base"]:
        raise ValueError("application MSP must use supported main SRAM")
    if layout["application_msp_end"] != layout["main_sram_supported_end"]:
        raise ValueError("application MSP must end at supported SRAM boundary")


LAYOUT = load_layout()
LAYOUT_PROFILES = {
    profile: load_layout(profile=profile)
    for profile in PROFILE_IDS
}
