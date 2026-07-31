#!/usr/bin/env python3
"""Run bounded host fuzz campaigns and write a reproducible JSON report."""

from __future__ import annotations

import argparse
import importlib.util
import json
import os
import subprocess  # nosec B404
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CORPUS_ROOT = ROOT / "fuzz" / "corpus"


def read_corpus(name: str) -> bytes:
    data = bytearray()
    for path in sorted((CORPUS_ROOT / name).glob("*.hex")):
        text = "".join(path.read_text(encoding="ascii").split())
        if text:
            data.extend(bytes.fromhex(text))
        data.extend(b"\x00")
    return bytes(data)


def valid_package_seed() -> bytes:
    signer_path = ROOT / "firmware" / "exp065_signed_app" / "tools" / "build_signed_image.py"
    spec = importlib.util.spec_from_file_location("fuzz_seed_signer", signer_path)
    if spec is None or spec.loader is None:
        raise RuntimeError("cannot load the repository test signer")
    signer = importlib.util.module_from_spec(spec)
    sys.path.insert(0, str(ROOT / "tools"))
    spec.loader.exec_module(signer)
    payload = bytearray(b"\xA5" * 128)
    vector_base = signer.LAYOUT["slot_b_payload_base"]
    signer.struct.pack_into("<II", payload, 0, signer.LAYOUT["application_msp_end"], vector_base | 1)
    package, _, _, _ = signer.build_update_package(
        bytes(payload), bytes(range(32)), slot="b", image_version=3
    )
    return package


def run_campaign(driver: Path, mode: str, iterations: int, seed: int) -> dict[str, object]:
    corpus_name = {
        "uart": "uart",
        "metadata": "metadata",
        "package": "update_package",
        "policy": "metadata",
    }[mode]
    corpus = read_corpus(corpus_name)
    if mode == "package":
        corpus = valid_package_seed()
    with tempfile.TemporaryDirectory(prefix="stm32-fuzz-") as temporary:
        input_path = Path(temporary) / "corpus.bin"
        input_path.write_bytes(corpus)
        command = [
            str(driver),
            "--mode",
            mode,
            "--input",
            str(input_path),
            "--iterations",
            str(iterations),
            "--seed",
            hex(seed),
        ]
        started = time.monotonic()
        completed = subprocess.run(  # nosec B603
            command,
            cwd=ROOT,
            capture_output=True,
            text=True,
            check=False,
            env={**os.environ, "ASAN_OPTIONS": "detect_leaks=1:halt_on_error=1"},
        )
        duration_seconds = time.monotonic() - started
    return {
        "mode": mode,
        "iterations": iterations,
        "seed": seed,
        "corpus_bytes": len(corpus),
        "duration_seconds": round(duration_seconds, 3),
        "returncode": completed.returncode,
        "stdout": completed.stdout.strip(),
        "stderr": completed.stderr.strip(),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--driver",
        type=Path,
        default=ROOT / "fuzz" / "build" / "host_fuzz_driver",
    )
    parser.add_argument("--iterations", type=int, default=10000)
    parser.add_argument("--output", type=Path)
    parser.add_argument("modes", nargs="*", default=["uart", "metadata", "package", "policy"])
    args = parser.parse_args()

    results = [
        run_campaign(args.driver, mode, args.iterations, 0xC0DEC0DE + index)
        for index, mode in enumerate(args.modes)
    ]
    report = {"engine": "portable-deterministic", "results": results}
    encoded = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if args.output is not None:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(encoded, encoding="ascii")
    print(encoded, end="")
    return 0 if all(result["returncode"] == 0 for result in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
