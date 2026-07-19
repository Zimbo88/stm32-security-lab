import re
import subprocess
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from stm32f429_layout import LAYOUT  # noqa: E402


def _read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="ascii")


class MemoryLayoutTests(unittest.TestCase):
    def test_generated_memory_layout_files_are_current(self) -> None:
        result = subprocess.run(
            [sys.executable, "tools/emit_memory_layout.py", "--check"],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(
            result.returncode,
            0,
            result.stdout + result.stderr,
        )

    def test_authoritative_layout_values_match_expected_target(self) -> None:
        self.assertEqual(LAYOUT["target"], "STM32F429IGT6")
        self.assertEqual(LAYOUT["flash_base"], 0x08000000)
        self.assertEqual(LAYOUT["flash_end"], 0x08200000)
        self.assertEqual(LAYOUT["flash_bank1_base"], 0x08000000)
        self.assertEqual(LAYOUT["flash_bank1_end"], 0x08100000)
        self.assertEqual(LAYOUT["flash_bank2_base"], 0x08100000)
        self.assertEqual(LAYOUT["flash_bank2_end"], 0x08200000)
        self.assertEqual(LAYOUT["bootloader_base"], 0x08000000)
        self.assertEqual(LAYOUT["bootloader_end"], 0x08008000)
        self.assertEqual(LAYOUT["boot_metadata_a_base"], 0x08008000)
        self.assertEqual(LAYOUT["boot_metadata_a_end"], 0x0800C000)
        self.assertEqual(LAYOUT["boot_metadata_b_base"], 0x0800C000)
        self.assertEqual(LAYOUT["boot_metadata_b_end"], 0x08010000)
        self.assertEqual(LAYOUT["update_metadata_base"], 0x08010000)
        self.assertEqual(LAYOUT["update_metadata_end"], 0x08020000)
        self.assertEqual(LAYOUT["slot_a_signed_image_base"], 0x08020000)
        self.assertEqual(LAYOUT["slot_a_payload_base"], 0x08020200)
        self.assertEqual(LAYOUT["slot_a_end"], 0x08100000)
        self.assertEqual(LAYOUT["slot_b_signed_image_base"], 0x08100000)
        self.assertEqual(LAYOUT["slot_b_payload_base"], 0x08100200)
        self.assertEqual(LAYOUT["slot_b_end"], 0x081E0000)
        self.assertEqual(LAYOUT["recovery_base"], 0x081E0000)
        self.assertEqual(LAYOUT["recovery_end"], 0x08200000)
        self.assertEqual(LAYOUT["signed_image_base"], 0x08020000)
        self.assertEqual(LAYOUT["signature_base"], 0x08020060)
        self.assertEqual(LAYOUT["application_base"], 0x08020200)
        self.assertEqual(LAYOUT["application_flash_end"], 0x08100000)
        self.assertEqual(LAYOUT["application_payload_max_size"], 0x000DFE00)
        self.assertEqual(LAYOUT["slot_a_payload_max_size"], 0x000DFE00)
        self.assertEqual(LAYOUT["slot_b_payload_max_size"], 0x000DFE00)
        self.assertEqual(LAYOUT["application_msp_base"], 0x20000000)
        self.assertEqual(LAYOUT["application_msp_end"], 0x20020000)
        self.assertFalse(LAYOUT["ccm_application_supported"])
        self.assertFalse(LAYOUT["sram_execution_supported"])

    def test_authoritative_sector_map_matches_stm32f429_dual_bank_geometry(self) -> None:
        sectors = LAYOUT["flash_sectors"]
        expected_sizes = [0x4000] * 4 + [0x10000] + [0x20000] * 7
        expected_sizes += [0x4000] * 4 + [0x10000] + [0x20000] * 7
        self.assertEqual([sector["id"] for sector in sectors], list(range(24)))
        address = 0x08000000
        for sector, size in zip(sectors, expected_sizes, strict=True):
            with self.subTest(sector=sector["id"]):
                self.assertEqual(sector["base"], address)
                self.assertEqual(sector["size"], size)
                self.assertEqual(sector["end"], address + size)
                self.assertEqual(sector["bank"], 1 if sector["id"] < 12 else 2)
            address += size
        self.assertEqual(address, 0x08200000)

    def test_named_regions_partition_physical_flash(self) -> None:
        ranges = [
            ("bootloader", LAYOUT["bootloader_base"], LAYOUT["bootloader_end"]),
            ("metadata_a", LAYOUT["boot_metadata_a_base"], LAYOUT["boot_metadata_a_end"]),
            ("metadata_b", LAYOUT["boot_metadata_b_base"], LAYOUT["boot_metadata_b_end"]),
            ("update_metadata", LAYOUT["update_metadata_base"], LAYOUT["update_metadata_end"]),
            ("slot_a", LAYOUT["slot_a_signed_image_base"], LAYOUT["slot_a_end"]),
            ("slot_b", LAYOUT["slot_b_signed_image_base"], LAYOUT["slot_b_end"]),
            ("recovery", LAYOUT["recovery_base"], LAYOUT["recovery_end"]),
        ]
        cursor = LAYOUT["flash_base"]
        for name, start, end in ranges:
            with self.subTest(region=name):
                self.assertEqual(start, cursor)
                self.assertGreater(end, start)
            cursor = end
        self.assertEqual(cursor, LAYOUT["flash_end"])

    def test_linker_scripts_include_generated_layout(self) -> None:
        bootloader = _read("firmware/exp045_bootloader_v2/linker.ld")
        app = _read("firmware/exp065_signed_app/linker.ld")
        platform = _read("firmware/exp066_research_platform_core/linker.ld")

        for text in (bootloader, app, platform):
            self.assertIn("INCLUDE ../common/stm32f429_memory_layout.ld", text)

        self.assertIn("ORIGIN = STM32F429_BOOTLOADER_BASE", bootloader)
        self.assertIn("LENGTH = STM32F429_BOOTLOADER_SIZE", bootloader)
        self.assertIn("ORIGIN = STM32F429_APPLICATION_BASE", app)
        self.assertIn("LENGTH = STM32F429_APPLICATION_PAYLOAD_MAX_SIZE", app)
        self.assertIn(
            "STM32F429_SELECTED_APPLICATION_BASE : STM32F429_APPLICATION_BASE",
            platform,
        )
        self.assertIn(
            "STM32F429_SELECTED_APPLICATION_PAYLOAD_MAX_SIZE :",
            platform,
        )
        self.assertIn(
            "ORIGIN = STM32F429_SELECTED_APPLICATION_BASE",
            platform,
        )
        self.assertIn(
            "LENGTH = STM32F429_SELECTED_APPLICATION_PAYLOAD_MAX_SIZE",
            platform,
        )
        self.assertIn(
            "ADDR(.isr_vector) == STM32F429_SELECTED_APPLICATION_BASE",
            platform,
        )

    def test_verifier_and_signer_use_supported_application_sram_bounds(self) -> None:
        board = _read("firmware/exp045_bootloader_v2/include/board.h")
        flash_layout = _read("firmware/exp045_bootloader_v2/src/flash_layout.h")
        signer = _read("firmware/exp065_signed_app/tools/build_signed_image.py")

        self.assertIn(
            "#define BOARD_SRAM_SIZE         STM32F429_MAIN_SRAM_SUPPORTED_SIZE",
            board,
        )
        self.assertNotRegex(board, re.compile(r"BOARD_SRAM_SIZE\s+\(256UL"))
        self.assertIn(
            "#define APPLICATION_MSP_END       STM32F429_APPLICATION_MSP_END",
            flash_layout,
        )
        self.assertIn('APPLICATION_MSP_END = LAYOUT["application_msp_end"]', signer)
        self.assertNotIn("0x20040000", signer)

    def test_flash_payload_bounds_have_one_source(self) -> None:
        flash_layout = _read("firmware/exp045_bootloader_v2/src/flash_layout.h")
        signer = _read("firmware/exp065_signed_app/tools/build_signed_image.py")
        makefile = _read("firmware/exp066_research_platform_core/Makefile")

        self.assertIn(
            "#define MAX_PAYLOAD_SIZE          STM32F429_APPLICATION_PAYLOAD_MAX_SIZE",
            flash_layout,
        )
        self.assertIn('APPLICATION_BASE = LAYOUT["application_base"]', signer)
        self.assertIn("#define STM32F429_SLOT_A_PAYLOAD_BASE 0x08020200UL", _read("firmware/common/stm32f429_memory_layout.h"))
        self.assertIn("#define STM32F429_SLOT_B_PAYLOAD_BASE 0x08100200UL", _read("firmware/common/stm32f429_memory_layout.h"))
        self.assertIn(
            "STM32F429_SLOT_A_APPLICATION_BASE_HEX",
            makefile,
        )
        self.assertIn(
            "STM32F429_SLOT_B_APPLICATION_BASE_HEX",
            makefile,
        )


if __name__ == "__main__":
    unittest.main()
