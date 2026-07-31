#!/usr/bin/env python3
"""Generate a deterministic SPDX 2.3 inventory from repository inputs."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess  # nosec B404 - fixed git query without shell execution
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
SPDX_VERSION = "SPDX-2.3"
LICENSE_MAP = {
    "pytest": "MIT",
    "pynacl": "Apache-2.0",
    "pyserial": "BSD-3-Clause",
    "hypothesis": "MPL-2.0",
    "coverage": "Apache-2.0",
    "ruff": "MIT",
    "mypy": "MIT",
    "bandit": "Apache-2.0",
    "pyyaml": "MIT",
}


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def git_commit() -> str:
    return subprocess.run(  # nosec B603 - fixed git argv and repository cwd
        ["git", "rev-parse", "HEAD"],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()


def parse_requirements(path: Path) -> list[tuple[str, str]]:
    result: list[tuple[str, str]] = []
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.split("#", 1)[0].strip()
        if not line or line.startswith("-"):
            continue
        match = re.fullmatch(r"([A-Za-z0-9_.-]+)==([^;\s]+)", line)
        if match is None:
            raise ValueError(f"unparseable pinned requirement in {path}: {raw}")
        result.append((match.group(1).lower(), match.group(2)))
    return result


def package_record(
    *, name: str, version: str, license_id: str, download: str, supplier: str
) -> dict[str, Any]:
    safe_name = re.sub(r"[^A-Za-z0-9.-]+", "-", name)
    return {
        "SPDXID": f"SPDXRef-Package-{safe_name}",
        "name": name,
        "versionInfo": version,
        "downloadLocation": download,
        "licenseConcluded": license_id,
        "licenseDeclared": license_id,
        "copyrightText": "NOASSERTION",
        "supplier": supplier,
        "filesAnalyzed": False,
    }


def build_document(args: argparse.Namespace) -> dict[str, Any]:
    commit = args.source_commit or git_commit()
    packages = [
        package_record(
            name="STM32 Security Lab",
            version=args.release_version,
            license_id="BSD-3-Clause",
            download="https://github.com/Zimbo88/stm32-security-lab",
            supplier="Organization: Zimbo88",
        ),
        package_record(
            name="Monocypher",
            version="4.0.3",
            license_id="LicenseRef-Monocypher-Dual-BSD-CC0",
            download="https://github.com/LoupVaillant/Monocypher/archive/refs/tags/4.0.3.tar.gz",
            supplier="Organization: Loup Vaillant",
        ),
    ]
    for requirements in (ROOT / "requirements.txt", ROOT / "requirements-security.txt"):
        for name, version in parse_requirements(requirements):
            if any(item["name"].lower() == name for item in packages):
                continue
            license_id = LICENSE_MAP.get(name, "NOASSERTION")
            packages.append(
                package_record(
                    name=name,
                    version=version,
                    license_id=license_id,
                    download=f"https://pypi.org/project/{name}/{version}/",
                    supplier="Organization: PyPI project",
                )
            )

    archive = ROOT / "third_party" / "monocypher-4.0.3.tar.gz"
    files = [
        {
            "fileName": "third_party/monocypher-4.0.3.tar.gz",
            "checksums": [{"algorithm": "SHA256", "checksumValue": sha256(archive)}],
            "licenseConcluded": "LicenseRef-Monocypher-Dual-BSD-CC0",
            "licenseInfoInFiles": ["LicenseRef-Monocypher-Dual-BSD-CC0"],
        }
    ]
    packages.sort(key=lambda item: item["name"].lower())
    relationships = [
        {
            "spdxElementId": "SPDXRef-Package-STM32-Security-Lab",
            "relationshipType": "DESCRIBES",
            "relatedSpdxElement": package["SPDXID"],
        }
        for package in packages[1:]
    ]
    return {
        "spdxVersion": SPDX_VERSION,
        "dataLicense": "CC0-1.0",
        "SPDXID": "SPDXRef-DOCUMENT",
        "name": f"stm32-security-lab-{args.release_version}",
        "documentNamespace": f"https://github.com/Zimbo88/stm32-security-lab/sbom/{commit}",
        "creationInfo": {
            "created": "1970-01-01T00:00:00Z",
            "creators": ["Tool: tools/generate_sbom.py"],
        },
        "packages": packages,
        "files": files,
        "relationships": relationships,
        "annotations": [
            {
                "annotationDate": "1970-01-01T00:00:00Z",
                "annotationType": "OTHER",
                "annotator": "Tool: tools/generate_sbom.py",
                "comment": "Build and test dependency versions are read from pinned repository requirement files.",
            }
        ],
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--release-version", default="unreleased")
    parser.add_argument("--source-commit")
    args = parser.parse_args()
    document = build_document(args)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(document, indent=2, sort_keys=True) + "\n", encoding="ascii")
    print(json.dumps({"result": "ok", "format": SPDX_VERSION, "path": args.output.as_posix()}, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
