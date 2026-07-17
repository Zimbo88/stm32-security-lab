"""EXP068 sandboxed bytecode VM.

This VM is intentionally small and deterministic.  It executes only the fixed
EXP068 bytecode format, has no host memory access, has no native-module path,
and exposes only capability-gated syscalls implemented by ``SyscallHost``.
"""

from __future__ import annotations

import argparse
import struct
from collections.abc import Iterable, Sequence
from dataclasses import dataclass
from pathlib import Path

from module_format import TYPE_BYTECODE, parse_package

BYTECODE_MAGIC = b"BCV1"
BYTECODE_VERSION = 1
BYTECODE_HEADER_FORMAT = "<4sHHHHHH"
BYTECODE_HEADER_SIZE = struct.calcsize(BYTECODE_HEADER_FORMAT)
assert BYTECODE_HEADER_SIZE == 16

INSTRUCTION_FORMAT = "<BBh"
INSTRUCTION_SIZE = struct.calcsize(INSTRUCTION_FORMAT)
assert INSTRUCTION_SIZE == 4

MAX_CODE_SIZE = 4096
MAX_STACK_LIMIT = 64
MAX_SYSCALL_ARGS = 4
UINT32_MASK = 0xFFFFFFFF

CAP_RCC_READ = 0x00006801
CAP_OUTPUT = 0x00006802
SUPPORTED_VM_CAPABILITIES = frozenset({CAP_RCC_READ, CAP_OUTPUT})

SYS_RCC_READ = 1
SYS_EMIT = 2

RCC_REGISTER_NAMES = ("RCC_CR", "RCC_CFGR", "RCC_CSR")

OP_HALT = 0x00
OP_PUSH_U16 = 0x01
OP_DROP = 0x02
OP_DUP = 0x03
OP_SWAP = 0x04
OP_ADD = 0x10
OP_SUB = 0x11
OP_AND = 0x12
OP_OR = 0x13
OP_XOR = 0x14
OP_SHL_IMM = 0x15
OP_SHR_IMM = 0x16
OP_NEZ = 0x17
OP_EQ = 0x18
OP_JMP_REL = 0x20
OP_JZ_REL = 0x21
OP_SYSCALL = 0x30


class VMError(ValueError):
    """Raised when bytecode validation or execution fails closed."""


@dataclass(frozen=True)
class Instruction:
    opcode: int
    operand: int
    immediate: int


@dataclass(frozen=True)
class BytecodeProgram:
    entry_offset: int
    max_stack: int
    instructions: tuple[Instruction, ...]


@dataclass(frozen=True)
class ExecutionResult:
    halted: bool
    instructions_executed: int
    outputs: tuple[tuple[int, int], ...]
    stack: tuple[int, ...]


def _u32(value: int) -> int:
    return value & UINT32_MASK


def _require_capability(capabilities: frozenset[int], capability: int) -> None:
    if capability not in capabilities:
        raise VMError(f"missing capability 0x{capability:08X}")


def encode_program(
    instructions: Sequence[Instruction],
    *,
    entry_offset: int = 0,
    max_stack: int = 16,
) -> bytes:
    if not 0 < max_stack <= MAX_STACK_LIMIT:
        raise VMError("invalid stack limit")
    code_size = len(instructions) * INSTRUCTION_SIZE
    if code_size == 0 or code_size > MAX_CODE_SIZE:
        raise VMError("invalid code size")
    if entry_offset % INSTRUCTION_SIZE != 0 or entry_offset >= code_size:
        raise VMError("invalid entry offset")

    code = bytearray()
    for instruction in instructions:
        if instruction.opcode < 0 or instruction.opcode > 0xFF:
            raise VMError("invalid opcode")
        if instruction.operand < 0 or instruction.operand > 0xFF:
            raise VMError("invalid operand")
        if instruction.immediate < -32768 or instruction.immediate > 32767:
            raise VMError("invalid immediate")
        code.extend(struct.pack(INSTRUCTION_FORMAT, instruction.opcode, instruction.operand, instruction.immediate))

    header = struct.pack(
        BYTECODE_HEADER_FORMAT,
        BYTECODE_MAGIC,
        BYTECODE_VERSION,
        BYTECODE_HEADER_SIZE,
        code_size,
        entry_offset,
        max_stack,
        0,
    )
    return header + bytes(code)


def parse_program(blob: bytes) -> BytecodeProgram:
    blob = bytes(blob)
    if len(blob) < BYTECODE_HEADER_SIZE:
        raise VMError("truncated bytecode header")

    magic, version, header_size, code_size, entry_offset, max_stack, reserved = struct.unpack_from(
        BYTECODE_HEADER_FORMAT,
        blob,
        0,
    )
    if magic != BYTECODE_MAGIC or version != BYTECODE_VERSION or header_size != BYTECODE_HEADER_SIZE:
        raise VMError("invalid bytecode identity")
    if reserved != 0:
        raise VMError("reserved bytecode header field")
    if code_size == 0 or code_size > MAX_CODE_SIZE:
        raise VMError("invalid code size")
    if len(blob) != BYTECODE_HEADER_SIZE + code_size:
        raise VMError("bytecode size mismatch")
    if code_size % INSTRUCTION_SIZE != 0:
        raise VMError("unaligned bytecode")
    if entry_offset % INSTRUCTION_SIZE != 0 or entry_offset >= code_size:
        raise VMError("invalid entry offset")
    if not 0 < max_stack <= MAX_STACK_LIMIT:
        raise VMError("invalid stack limit")

    instructions = []
    for offset in range(BYTECODE_HEADER_SIZE, len(blob), INSTRUCTION_SIZE):
        opcode, operand, immediate = struct.unpack_from(INSTRUCTION_FORMAT, blob, offset)
        instruction = Instruction(opcode, operand, immediate)
        _validate_instruction(instruction)
        instructions.append(instruction)

    return BytecodeProgram(entry_offset, max_stack, tuple(instructions))


def _validate_instruction(instruction: Instruction) -> None:
    opcode = instruction.opcode
    if opcode in {
        OP_HALT,
        OP_PUSH_U16,
        OP_DROP,
        OP_DUP,
        OP_SWAP,
        OP_ADD,
        OP_SUB,
        OP_AND,
        OP_OR,
        OP_XOR,
        OP_SHL_IMM,
        OP_SHR_IMM,
        OP_NEZ,
        OP_EQ,
        OP_JMP_REL,
        OP_JZ_REL,
        OP_SYSCALL,
    }:
        pass
    else:
        raise VMError(f"unsupported opcode 0x{opcode:02X}")

    if opcode in {OP_SHL_IMM, OP_SHR_IMM} and instruction.operand > 31:
        raise VMError("invalid shift amount")
    if opcode == OP_SYSCALL and instruction.operand > MAX_SYSCALL_ARGS:
        raise VMError("too many syscall arguments")


class SyscallHost:
    """Curated EXP068 syscall implementation.

    The host accepts RCC values directly as data.  It never receives or reads an
    address from bytecode; the bytecode can only request fixed RCC indices.
    """

    def __init__(self, *, rcc: Sequence[int] = (0, 0, 0)) -> None:
        if len(rcc) != len(RCC_REGISTER_NAMES):
            raise ValueError("RCC snapshot must contain CR, CFGR, and CSR")
        self.rcc = tuple(_u32(int(value)) for value in rcc)
        self.outputs: list[tuple[int, int]] = []

    def handle(self, syscall_id: int, args: Sequence[int], capabilities: frozenset[int]) -> tuple[int, ...]:
        if syscall_id == SYS_RCC_READ:
            _require_capability(capabilities, CAP_RCC_READ)
            if len(args) != 1:
                raise VMError("RCC_READ expects one argument")
            index = args[0]
            if index >= len(self.rcc):
                raise VMError("invalid RCC register index")
            return (self.rcc[index],)

        if syscall_id == SYS_EMIT:
            _require_capability(capabilities, CAP_OUTPUT)
            if len(args) != 2:
                raise VMError("EMIT expects two arguments")
            value, channel = args
            self.outputs.append((_u32(channel), _u32(value)))
            return ()

        raise VMError(f"unsupported syscall {syscall_id}")


class BytecodeVM:
    def __init__(
        self,
        *,
        instruction_budget: int = 256,
        max_stack: int = MAX_STACK_LIMIT,
        host: SyscallHost | None = None,
    ) -> None:
        if instruction_budget <= 0:
            raise ValueError("instruction budget must be positive")
        if not 0 < max_stack <= MAX_STACK_LIMIT:
            raise ValueError("invalid VM stack limit")
        self.instruction_budget = instruction_budget
        self.max_stack = max_stack
        self.host = host if host is not None else SyscallHost()

    def execute(self, blob: bytes, *, capabilities: Iterable[int]) -> ExecutionResult:
        self.host.outputs.clear()
        program = parse_program(blob)
        stack_limit = min(program.max_stack, self.max_stack)
        capabilities_set = frozenset(int(cap) for cap in capabilities)

        pc = program.entry_offset // INSTRUCTION_SIZE
        stack: list[int] = []
        executed = 0

        def push(value: int) -> None:
            if len(stack) >= stack_limit:
                raise VMError("stack overflow")
            stack.append(_u32(value))

        def pop() -> int:
            if not stack:
                raise VMError("stack underflow")
            return stack.pop()

        while True:
            if executed >= self.instruction_budget:
                raise VMError("instruction budget exhausted")
            if pc < 0 or pc >= len(program.instructions):
                raise VMError("program counter out of range")

            instruction = program.instructions[pc]
            executed += 1
            pc += 1

            opcode = instruction.opcode
            if opcode == OP_HALT:
                return ExecutionResult(
                    True,
                    executed,
                    tuple(self.host.outputs),
                    tuple(stack),
                )
            if opcode == OP_PUSH_U16:
                push(instruction.immediate & 0xFFFF)
            elif opcode == OP_DROP:
                pop()
            elif opcode == OP_DUP:
                value = pop()
                push(value)
                push(value)
            elif opcode == OP_SWAP:
                if len(stack) < 2:
                    raise VMError("stack underflow")
                stack[-1], stack[-2] = stack[-2], stack[-1]
            elif opcode == OP_ADD:
                right = pop()
                left = pop()
                push(left + right)
            elif opcode == OP_SUB:
                right = pop()
                left = pop()
                push(left - right)
            elif opcode == OP_AND:
                right = pop()
                left = pop()
                push(left & right)
            elif opcode == OP_OR:
                right = pop()
                left = pop()
                push(left | right)
            elif opcode == OP_XOR:
                right = pop()
                left = pop()
                push(left ^ right)
            elif opcode == OP_SHL_IMM:
                push(pop() << instruction.operand)
            elif opcode == OP_SHR_IMM:
                push(pop() >> instruction.operand)
            elif opcode == OP_NEZ:
                push(1 if pop() != 0 else 0)
            elif opcode == OP_EQ:
                right = pop()
                left = pop()
                push(1 if left == right else 0)
            elif opcode == OP_JMP_REL:
                pc = _branch_target(pc, instruction.immediate, len(program.instructions))
            elif opcode == OP_JZ_REL:
                value = pop()
                if value == 0:
                    pc = _branch_target(pc, instruction.immediate, len(program.instructions))
            elif opcode == OP_SYSCALL:
                argc = instruction.operand
                if len(stack) < argc:
                    raise VMError("stack underflow")
                args = stack[len(stack) - argc :]
                del stack[len(stack) - argc :]
                results = self.host.handle(instruction.immediate & 0xFFFF, args, capabilities_set)
                for result in results:
                    push(result)
            else:
                raise VMError(f"unsupported opcode 0x{opcode:02X}")


def _branch_target(current_pc: int, relative: int, instruction_count: int) -> int:
    target = current_pc + relative
    if target < 0 or target >= instruction_count:
        raise VMError("branch target out of range")
    return target


def run_package(
    package: bytes,
    public_key: bytes | None,
    *,
    min_platform: int = 1,
    abi_version: int = 1,
    min_module_version: int = 0,
    instruction_budget: int = 256,
    host: SyscallHost | None = None,
) -> ExecutionResult:
    manifest = parse_package(
        package,
        public_key,
        min_platform=min_platform,
        abi_version=abi_version,
        min_module_version=min_module_version,
        supported_capabilities=SUPPORTED_VM_CAPABILITIES,
    )
    if manifest.header.module_type != TYPE_BYTECODE:
        raise VMError("native modules are not supported in EXP068")
    if manifest.header.entry_offset != 0:
        raise VMError("EXP068 package entry_offset must be zero")

    payload = package[manifest.header.payload_offset : manifest.header.payload_offset + manifest.header.payload_size]
    vm = BytecodeVM(instruction_budget=instruction_budget, host=host)
    return vm.execute(payload, capabilities=manifest.capabilities)


def _u32_arg(text: str) -> int:
    try:
        value = int(text, 0)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(str(exc)) from exc
    if value < 0 or value > UINT32_MASK:
        raise argparse.ArgumentTypeError("value must fit uint32")
    return value


def _load_public_key(value: str) -> bytes:
    path = Path(value)
    if path.exists():
        data = path.read_bytes()
        if len(data) == 32:
            return data
        value = data.decode("ascii").strip()
    try:
        key = bytes.fromhex(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("public key must be 32 raw bytes or hex") from exc
    if len(key) != 32:
        raise argparse.ArgumentTypeError("public key must be 32 bytes")
    return key


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Run EXP068 bytecode or a signed bytecode package")
    parser.add_argument("input", type=Path)
    parser.add_argument("--package", action="store_true", help="treat input as an EXP067/EXP068 signed package")
    parser.add_argument("--public-key", type=_load_public_key)
    parser.add_argument("--cap", type=_u32_arg, action="append", default=[])
    parser.add_argument("--budget", type=_u32_arg, default=256)
    parser.add_argument("--rcc-cr", type=_u32_arg, default=0)
    parser.add_argument("--rcc-cfgr", type=_u32_arg, default=0)
    parser.add_argument("--rcc-csr", type=_u32_arg, default=0)
    args = parser.parse_args(argv)

    host = SyscallHost(rcc=(args.rcc_cr, args.rcc_cfgr, args.rcc_csr))
    data = args.input.read_bytes()
    if args.package:
        result = run_package(
            data,
            args.public_key,
            instruction_budget=args.budget,
            host=host,
        )
    else:
        result = BytecodeVM(instruction_budget=args.budget, host=host).execute(data, capabilities=args.cap)

    print(f"halted={str(result.halted).lower()}")
    print(f"instructions={result.instructions_executed}")
    for channel, value in result.outputs:
        print(f"out {channel} 0x{value:08X}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
