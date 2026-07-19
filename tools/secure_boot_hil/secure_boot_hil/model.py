"""Typed data model for secure-boot HIL execution."""

from __future__ import annotations

from dataclasses import dataclass, field
from enum import StrEnum
from pathlib import Path
from typing import Any, ClassVar


class Verdict(StrEnum):
    PASS = "PASS"
    FAIL = "FAIL"
    ERROR = "ERROR"
    SKIP = "SKIP"
    OBSERVE = "OBSERVE"


class AssertionKind(StrEnum):
    LITERAL = "literal"
    REGEX = "regex"
    FIELD_EQUALS = "field_equals"


class MutationType(StrEnum):
    BAD_SIGNATURE = "bad_signature"
    MODIFIED_PAYLOAD = "modified_payload"
    ERASED_HEADER = "erased_header"
    ZERO_HEADER = "zero_header"
    ERASED_METADATA = "erased_metadata"
    ZERO_METADATA = "zero_metadata"
    SEEDED_BIT_FLIP = "seeded_bit_flip"


class FlashOperation(StrEnum):
    WRITE = "write"
    ERASE = "erase"


@dataclass(frozen=True)
class FlashRegion:
    name: str
    address: int
    size: int

    @property
    def end(self) -> int:
        return self.address + self.size

    def contains(self, address: int, size: int) -> bool:
        return self.address <= address and address + size <= self.end

    def to_json(self) -> dict[str, Any]:
        return {
            "name": self.name,
            "address": self.address,
            "address_hex": f"0x{self.address:08X}",
            "size": self.size,
            "end": self.end,
            "end_hex": f"0x{self.end:08X}",
        }


@dataclass(frozen=True)
class CommandResult:
    command: list[str]
    cwd: str
    start_utc: str
    end_utc: str
    duration_seconds: float
    returncode: int
    stdout: str
    stderr: str
    timed_out: bool = False

    def to_json(self) -> dict[str, Any]:
        return {
            "command": self.command,
            "cwd": self.cwd,
            "start_utc": self.start_utc,
            "end_utc": self.end_utc,
            "duration_seconds": self.duration_seconds,
            "returncode": self.returncode,
            "stdout": self.stdout,
            "stderr": self.stderr,
            "timed_out": self.timed_out,
        }


@dataclass(frozen=True)
class Artifact:
    name: str
    path: Path
    size: int
    sha256: str
    role: str

    def to_json(self) -> dict[str, Any]:
        return {
            "name": self.name,
            "path": self.path.as_posix(),
            "size": self.size,
            "sha256": self.sha256,
            "role": self.role,
        }


@dataclass(frozen=True)
class BackupRecord:
    region: FlashRegion
    path: Path
    size: int
    sha256: str
    created_utc: str

    def to_json(self) -> dict[str, Any]:
        return {
            "region": self.region.to_json(),
            "path": self.path.as_posix(),
            "actual_size": self.size,
            "sha256": self.sha256,
            "created_utc": self.created_utc,
        }


@dataclass(frozen=True)
class RestoreRecord:
    region: FlashRegion
    backup_sha256: str
    readback_path: Path
    readback_sha256: str
    matched: bool

    def to_json(self) -> dict[str, Any]:
        return {
            "region": self.region.to_json(),
            "backup_sha256": self.backup_sha256,
            "readback_path": self.readback_path.as_posix(),
            "readback_sha256": self.readback_sha256,
            "matched": self.matched,
        }


@dataclass(frozen=True)
class MutationRecord:
    source_path: Path
    output_path: Path
    mutation_type: MutationType
    source_sha256: str
    output_sha256: str
    byte_offset: int
    old_value: int
    new_value: int
    rationale: str

    def to_json(self) -> dict[str, Any]:
        return {
            "source_path": self.source_path.as_posix(),
            "output_path": self.output_path.as_posix(),
            "mutation_type": self.mutation_type.value,
            "source_sha256": self.source_sha256,
            "output_sha256": self.output_sha256,
            "byte_offset": self.byte_offset,
            "old_value": self.old_value,
            "new_value": self.new_value,
            "rationale": self.rationale,
        }


@dataclass(frozen=True)
class UartFrame:
    text: str
    complete: bool
    start_offset: int
    end_offset: int
    path: Path | None = None

    def to_json(self) -> dict[str, Any]:
        return {
            "text": self.text,
            "complete": self.complete,
            "start_offset": self.start_offset,
            "end_offset": self.end_offset,
            "path": self.path.as_posix() if self.path else None,
        }


@dataclass(frozen=True)
class AssertionSpec:
    kind: AssertionKind
    expression: str
    field: str | None = None
    expected: str | int | None = None

    def to_json(self) -> dict[str, Any]:
        return {
            "kind": self.kind.value,
            "expression": self.expression,
            "field": self.field,
            "expected": self.expected,
        }


@dataclass(frozen=True)
class AssertionResult:
    spec: AssertionSpec
    matched: bool
    excerpt: str | None = None

    def to_json(self) -> dict[str, Any]:
        return {
            "assertion": self.spec.to_json(),
            "matched": self.matched,
            "excerpt": self.excerpt,
        }


@dataclass(frozen=True)
class FlashAction:
    operation: FlashOperation
    region: str
    artifact: str | None = None
    fill: int | None = None

    def to_json(self) -> dict[str, Any]:
        return {
            "operation": self.operation.value,
            "region": self.region,
            "artifact": self.artifact,
            "fill": self.fill,
        }


@dataclass(frozen=True)
class TestCase:
    __test__: ClassVar[bool] = False

    identifier: str
    name: str
    objective: str
    preconditions: list[str]
    flash_actions: list[FlashAction]
    required_uart: list[AssertionSpec]
    forbidden_uart: list[AssertionSpec]
    expected_verdict: Verdict
    tags: list[str]
    timeout_seconds: float
    cleanup: str = "restore"
    evidence_paths: list[str] = field(default_factory=list)

    def to_json(self) -> dict[str, Any]:
        return {
            "identifier": self.identifier,
            "name": self.name,
            "objective": self.objective,
            "preconditions": self.preconditions,
            "flash_actions": [action.to_json() for action in self.flash_actions],
            "required_uart": [item.to_json() for item in self.required_uart],
            "forbidden_uart": [item.to_json() for item in self.forbidden_uart],
            "expected_verdict": self.expected_verdict.value,
            "tags": self.tags,
            "timeout_seconds": self.timeout_seconds,
            "cleanup": self.cleanup,
            "evidence_paths": self.evidence_paths,
        }


@dataclass(frozen=True)
class TestResult:
    __test__: ClassVar[bool] = False

    test: TestCase
    verdict: Verdict
    required: list[AssertionResult]
    forbidden: list[AssertionResult]
    uart_frame: UartFrame | None
    metrics: dict[str, float | int]
    start_utc: str
    end_utc: str
    duration_seconds: float
    error: str | None = None

    def to_json(self) -> dict[str, Any]:
        return {
            "test": self.test.to_json(),
            "verdict": self.verdict.value,
            "required": [item.to_json() for item in self.required],
            "forbidden": [item.to_json() for item in self.forbidden],
            "uart_frame": self.uart_frame.to_json() if self.uart_frame else None,
            "metrics": self.metrics,
            "start_utc": self.start_utc,
            "end_utc": self.end_utc,
            "duration_seconds": self.duration_seconds,
            "error": self.error,
        }
