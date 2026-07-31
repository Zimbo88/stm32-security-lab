import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / "tools" / "key_management.py"


def test_key_generation_is_non_overwriting_and_reports_only_public_data(tmp_path):
    seed = tmp_path / "research.seed"
    header = tmp_path / "firmware_public_key.h"
    result = subprocess.run(
        [sys.executable, str(TOOL), "generate", "--purpose", "ci-test",
         "--seed", str(seed), "--public-header", str(header)],
        text=True, capture_output=True, check=False,
    )
    assert result.returncode == 0, result.stderr
    assert seed.stat().st_mode & 0o077 == 0
    assert "public_key_sha256=" in result.stdout
    assert seed.read_bytes().hex() not in result.stdout
    second = subprocess.run(
        [sys.executable, str(TOOL), "generate", "--purpose", "ci-test",
         "--seed", str(seed), "--public-header", str(tmp_path / "other.h")],
        text=True, capture_output=True, check=False,
    )
    assert second.returncode != 0


def test_key_inspect_requires_private_seed_permissions(tmp_path):
    seed = tmp_path / "seed.bin"
    seed.write_bytes(b"\x01" * 32)
    os.chmod(seed, 0o644)
    result = subprocess.run(
        [sys.executable, str(TOOL), "inspect", "--seed", str(seed)],
        text=True, capture_output=True, check=False,
    )
    assert result.returncode != 0
    assert "0600" in result.stderr
