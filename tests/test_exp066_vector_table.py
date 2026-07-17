from __future__ import annotations

import shutil
import struct
import subprocess
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory


ROOT = Path(__file__).parents[1]
PROJECT = ROOT / "firmware" / "exp066_research_platform_core"
APPLICATION_BASE = 0x08008200


def _run(command: list[str], *, cwd: Path | None = None) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command,
        cwd=cwd,
        check=True,
        text=True,
        capture_output=True,
    )


def _build_project_copy(tmpdir: Path) -> Path:
    work = tmpdir / "exp066_research_platform_core"
    shutil.copytree(PROJECT, work)
    _run(["make", "-C", str(work), "all"])
    return work / "build" / "exp066_research_platform_core.elf"


def _symbols(elf: Path) -> dict[str, int]:
    result = _run(["arm-none-eabi-nm", "-n", str(elf)])
    symbols: dict[str, int] = {}
    for line in result.stdout.splitlines():
        parts = line.split()
        if len(parts) >= 3:
            symbols[parts[2]] = int(parts[0], 16)
    return symbols


def _vector_words(elf: Path, tmpdir: Path) -> tuple[int, ...]:
    vector_bin = tmpdir / "isr_vector.bin"
    _run(
        [
            "arm-none-eabi-objcopy",
            "-O",
            "binary",
            "-j",
            ".isr_vector",
            str(elf),
            str(vector_bin),
        ]
    )
    data = vector_bin.read_bytes()
    assert len(data) >= 64
    return struct.unpack("<16I", data[:64])


class Exp066VectorTableTests(unittest.TestCase):
    def test_cortex_m4_exception_vector_order(self) -> None:
        with TemporaryDirectory() as tmpdir_name:
            tmpdir = Path(tmpdir_name)
            elf = _build_project_copy(tmpdir)
            symbols = _symbols(elf)
            vectors = _vector_words(elf, tmpdir)

        self.assertEqual(symbols["vector_table"], APPLICATION_BASE)
        self.assertEqual(vectors[0], 0x20020000)

        expected = {
            1: "Reset_Handler",
            2: "NMI_Handler",
            3: "HardFault_Handler",
            4: "MemManage_Handler",
            5: "BusFault_Handler",
            6: "UsageFault_Handler",
            11: "SVCall_Handler",
            12: "DebugMon_Handler",
            14: "PendSV_Handler",
            15: "SysTick_Handler",
        }
        for index, symbol in expected.items():
            with self.subTest(index=index, symbol=symbol):
                self.assertEqual(vectors[index] & ~1, symbols[symbol] & ~1)

        for index in (7, 8, 9, 10, 13):
            with self.subTest(reserved_index=index):
                self.assertEqual(vectors[index], 0)

        self.assertEqual(vectors[3] & ~1, symbols["HardFault_Handler"] & ~1)
        self.assertNotEqual(vectors[3] & ~1, symbols["Default_Handler"] & ~1)


if __name__ == "__main__":
    unittest.main()
