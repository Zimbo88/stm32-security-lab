#!/usr/bin/env python3
"""Assembler for the EXP068 bytecode VM."""

from __future__ import annotations

import argparse
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Sequence

from bytecode_vm import (
    CAP_OUTPUT,
    CAP_RCC_READ,
    OP_ADD,
    OP_AND,
    OP_DROP,
    OP_DUP,
    OP_EQ,
    OP_HALT,
    OP_JMP_REL,
    OP_JZ_REL,
    OP_NEZ,
    OP_OR,
    OP_PUSH_U16,
    OP_SHL_IMM,
    OP_SHR_IMM,
    OP_SUB,
    OP_SWAP,
    OP_SYSCALL,
    OP_XOR,
    SYS_EMIT,
    SYS_RCC_READ,
    Instruction,
    encode_program,
)

SYSCALL_NAMES = {
    "RCC_READ": SYS_RCC_READ,
    "EMIT": SYS_EMIT,
}

CAPABILITY_NAMES = {
    "RCC_READ": CAP_RCC_READ,
    "OUTPUT": CAP_OUTPUT,
}

LABEL_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")


class AssemblerError(ValueError):
    pass


@dataclass(frozen=True)
class PendingInstruction:
    op: str
    args: tuple[str, ...]
    line_number: int


@dataclass(frozen=True)
class AssemblyResult:
    bytecode: bytes
    capabilities: tuple[int, ...]
    max_stack: int


def _strip_comment(line: str) -> str:
    for marker in (";", "#"):
        index = line.find(marker)
        if index >= 0:
            line = line[:index]
    return line.strip()


def _parse_int(text: str, *, signed: bool = False, bits: int = 32) -> int:
    try:
        value = int(text, 0)
    except ValueError as exc:
        raise AssemblerError(f"invalid integer {text!r}") from exc
    minimum = -(1 << (bits - 1)) if signed else 0
    maximum = (1 << (bits - 1)) - 1 if signed else (1 << bits) - 1
    if value < minimum or value > maximum:
        raise AssemblerError(f"integer {text!r} outside {bits}-bit range")
    return value


def _parse_capability(text: str) -> int:
    upper = text.upper()
    if upper in CAPABILITY_NAMES:
        return CAPABILITY_NAMES[upper]
    return _parse_int(text, bits=32)


def _u16_as_i16(value: int) -> int:
    return value if value <= 0x7FFF else value - 0x10000


def assemble_text(text: str) -> AssemblyResult:
    labels: dict[str, int] = {}
    pending: list[PendingInstruction] = []
    capabilities: list[int] = []
    max_stack = 16

    for line_number, raw_line in enumerate(text.splitlines(), start=1):
        line = _strip_comment(raw_line)
        if not line:
            continue

        if line.startswith("."):
            directive, *args = line.split()
            directive = directive.lower()
            if directive == ".stack":
                if len(args) != 1:
                    raise AssemblerError(f"line {line_number}: .stack expects one argument")
                max_stack = _parse_int(args[0], bits=16)
            elif directive == ".cap":
                if len(args) != 1:
                    raise AssemblerError(f"line {line_number}: .cap expects one argument")
                capabilities.append(_parse_capability(args[0]))
            else:
                raise AssemblerError(f"line {line_number}: unknown directive {directive}")
            continue

        if line.endswith(":"):
            label = line[:-1]
            if not LABEL_RE.match(label):
                raise AssemblerError(f"line {line_number}: invalid label {label!r}")
            if label in labels:
                raise AssemblerError(f"line {line_number}: duplicate label {label!r}")
            labels[label] = len(pending)
            continue

        parts = line.replace(",", " ").split()
        pending.append(PendingInstruction(parts[0].upper(), tuple(parts[1:]), line_number))

    if not pending:
        raise AssemblerError("program has no instructions")
    if len(set(capabilities)) != len(capabilities):
        raise AssemblerError("duplicate capability")

    instructions = tuple(_assemble_instruction(item, labels, pc) for pc, item in enumerate(pending))
    return AssemblyResult(encode_program(instructions, max_stack=max_stack), tuple(capabilities), max_stack)


def _assemble_instruction(
    item: PendingInstruction,
    labels: dict[str, int],
    pc: int,
) -> Instruction:
    op = item.op
    args = item.args
    line = item.line_number

    def no_args(opcode: int) -> Instruction:
        if args:
            raise AssemblerError(f"line {line}: {op} expects no arguments")
        return Instruction(opcode, 0, 0)

    if op == "HALT":
        return no_args(OP_HALT)
    if op == "DROP":
        return no_args(OP_DROP)
    if op == "DUP":
        return no_args(OP_DUP)
    if op == "SWAP":
        return no_args(OP_SWAP)
    if op == "ADD":
        return no_args(OP_ADD)
    if op == "SUB":
        return no_args(OP_SUB)
    if op == "AND":
        return no_args(OP_AND)
    if op == "OR":
        return no_args(OP_OR)
    if op == "XOR":
        return no_args(OP_XOR)
    if op == "NEZ":
        return no_args(OP_NEZ)
    if op == "EQ":
        return no_args(OP_EQ)

    if op == "PUSH":
        if len(args) != 1:
            raise AssemblerError(f"line {line}: PUSH expects one argument")
        return Instruction(OP_PUSH_U16, 0, _u16_as_i16(_parse_int(args[0], bits=16)))

    if op in ("SHL", "SHR"):
        if len(args) != 1:
            raise AssemblerError(f"line {line}: {op} expects one argument")
        shift = _parse_int(args[0], bits=8)
        if shift > 31:
            raise AssemblerError(f"line {line}: shift outside 0..31")
        return Instruction(OP_SHL_IMM if op == "SHL" else OP_SHR_IMM, shift, 0)

    if op in ("JMP", "JZ"):
        if len(args) != 1:
            raise AssemblerError(f"line {line}: {op} expects one label")
        if args[0] not in labels:
            raise AssemblerError(f"line {line}: unknown label {args[0]!r}")
        # VM branches are relative to the PC after fetching the branch.
        relative = labels[args[0]] - (pc + 1)
        if relative < -32768 or relative > 32767:
            raise AssemblerError(f"line {line}: branch target out of range")
        return Instruction(OP_JMP_REL if op == "JMP" else OP_JZ_REL, 0, relative)

    if op == "SYSCALL":
        if len(args) != 2:
            raise AssemblerError(f"line {line}: SYSCALL expects name/id and argc")
        syscall_name = args[0].upper()
        syscall_id = SYSCALL_NAMES[syscall_name] if syscall_name in SYSCALL_NAMES else _parse_int(args[0], bits=16)
        argc = _parse_int(args[1], bits=8)
        return Instruction(OP_SYSCALL, argc, _u16_as_i16(syscall_id))

    raise AssemblerError(f"line {line}: unknown instruction {op}")


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Assemble EXP068 bytecode")
    parser.add_argument("source", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument(
        "--print-caps",
        action="store_true",
        help="print capabilities declared by .cap directives",
    )
    args = parser.parse_args(argv)

    result = assemble_text(args.source.read_text())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(result.bytecode)
    print(f"wrote {len(result.bytecode)} bytes")
    if args.print_caps:
        for capability in result.capabilities:
            print(f"cap 0x{capability:08X}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
