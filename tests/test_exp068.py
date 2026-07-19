from __future__ import annotations

import subprocess
import sys
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

sys.path.insert(0, str(Path(__file__).parents[1] / "tools"))

from bytecode_asm import assemble_text
from bytecode_vm import (
    CAP_OUTPUT,
    CAP_RCC_READ,
    BytecodeVM,
    Instruction,
    SyscallHost,
    VMError,
    encode_program,
    run_package,
)
from module_format import TYPE_BYTECODE, TYPE_NATIVE, build_package
from nacl.signing import SigningKey

ROOT = Path(__file__).parents[1]
RCC_MODULE = ROOT / "modules" / "exp068_rcc_analysis.bcasm"


class Exp068BytecodeVMTests(unittest.TestCase):
    def test_rcc_analysis_module_outputs_curated_fields(self):
        bytecode = assemble_text(RCC_MODULE.read_text()).bytecode
        host = SyscallHost(rcc=(0x00000003, 0x00000004, 0x08000000))

        result = BytecodeVM(instruction_budget=64, host=host).execute(
            bytecode,
            capabilities={CAP_RCC_READ, CAP_OUTPUT},
        )

        self.assertTrue(result.halted)
        self.assertEqual(
            result.outputs,
            (
                (1, 0x00000003),
                (2, 1),
                (3, 0),
                (4, 1),
                (5, 0x08000000),
            ),
        )
        self.assertEqual(result.stack, ())

    def test_instruction_budget_is_enforced(self):
        program = assemble_text(
            """
            .stack 2
            loop:
            JMP loop
            """
        ).bytecode

        with self.assertRaisesRegex(VMError, "instruction budget"):
            BytecodeVM(instruction_budget=4).execute(program, capabilities=set())

    def test_stack_limit_is_enforced(self):
        program = assemble_text(
            """
            .stack 2
            PUSH 1
            PUSH 2
            PUSH 3
            HALT
            """
        ).bytecode

        with self.assertRaisesRegex(VMError, "stack overflow"):
            BytecodeVM(instruction_budget=8).execute(program, capabilities=set())

    def test_capability_enforcement_blocks_syscalls(self):
        bytecode = assemble_text(RCC_MODULE.read_text()).bytecode
        host = SyscallHost(rcc=(0x3, 0x0, 0x0))

        with self.assertRaisesRegex(VMError, "missing capability"):
            BytecodeVM(instruction_budget=64, host=host).execute(bytecode, capabilities={CAP_OUTPUT})

    def test_syscall_argument_validation(self):
        program = assemble_text(
            """
            .stack 2
            .cap RCC_READ
            PUSH 3
            SYSCALL RCC_READ 1
            HALT
            """
        ).bytecode

        with self.assertRaisesRegex(VMError, "invalid RCC register index"):
            BytecodeVM(instruction_budget=8).execute(program, capabilities={CAP_RCC_READ})

    def test_branch_target_validation_fails_closed(self):
        program = encode_program(
            (
                Instruction(0x20, 0, 10),
                Instruction(0x00, 0, 0),
            ),
            max_stack=1,
        )

        with self.assertRaisesRegex(VMError, "branch target"):
            BytecodeVM(instruction_budget=8).execute(program, capabilities=set())

    def test_package_runner_accepts_bytecode_and_rejects_native(self):
        key = SigningKey.generate()
        bytecode = assemble_text(RCC_MODULE.read_text()).bytecode
        package = build_package(
            bytecode,
            module_id=0x68,
            version=1,
            seed=bytes(key),
            capabilities=[CAP_RCC_READ, CAP_OUTPUT],
            module_type=TYPE_BYTECODE,
        )

        host = SyscallHost(rcc=(0x01000002, 0x00000008, 0x00000000))
        result = run_package(package, bytes(key.verify_key), instruction_budget=64, host=host)
        self.assertEqual(result.outputs[2], (3, 1))
        self.assertEqual(result.outputs[3], (4, 2))

        native_package = build_package(
            bytecode,
            module_id=0x69,
            version=1,
            seed=bytes(key),
            capabilities=[CAP_RCC_READ, CAP_OUTPUT],
            module_type=TYPE_NATIVE,
        )
        with self.assertRaisesRegex(VMError, "native modules"):
            run_package(native_package, bytes(key.verify_key))

    def test_assembler_and_runner_cli(self):
        with TemporaryDirectory() as tmpdir_name:
            tmpdir = Path(tmpdir_name)
            bytecode = tmpdir / "rcc.bc"

            asm = subprocess.run(
                [
                    sys.executable,
                    str(ROOT / "tools" / "bytecode_asm.py"),
                    str(RCC_MODULE),
                    "--output",
                    str(bytecode),
                    "--print-caps",
                ],
                check=True,
                text=True,
                capture_output=True,
            )
            self.assertIn("cap 0x00006801", asm.stdout)
            self.assertIn("cap 0x00006802", asm.stdout)

            run = subprocess.run(
                [
                    sys.executable,
                    str(ROOT / "tools" / "bytecode_vm.py"),
                    str(bytecode),
                    "--cap",
                    hex(CAP_RCC_READ),
                    "--cap",
                    hex(CAP_OUTPUT),
                    "--rcc-cr",
                    "0x3",
                    "--rcc-cfgr",
                    "0x4",
                    "--rcc-csr",
                    "0x08000000",
                ],
                check=True,
                text=True,
                capture_output=True,
            )
            self.assertIn("halted=true", run.stdout)
            self.assertIn("out 4 0x00000001", run.stdout)

    def test_package_runner_cli(self):
        key = SigningKey.generate()
        bytecode = assemble_text(RCC_MODULE.read_text()).bytecode
        package = build_package(
            bytecode,
            module_id=0x68,
            version=1,
            seed=bytes(key),
            capabilities=[CAP_RCC_READ, CAP_OUTPUT],
            module_type=TYPE_BYTECODE,
        )

        with TemporaryDirectory() as tmpdir_name:
            tmpdir = Path(tmpdir_name)
            package_path = tmpdir / "rcc.mod"
            package_path.write_bytes(package)

            run = subprocess.run(
                [
                    sys.executable,
                    str(ROOT / "tools" / "bytecode_vm.py"),
                    str(package_path),
                    "--package",
                    "--public-key",
                    bytes(key.verify_key).hex(),
                    "--rcc-cr",
                    "0x3",
                ],
                check=True,
                text=True,
                capture_output=True,
            )
            self.assertIn("out 2 0x00000001", run.stdout)

    def test_push_uint16_full_range(self):
        program = assemble_text(
            """
            .stack 1
            PUSH 0xFFFF
            HALT
            """
        ).bytecode

        result = BytecodeVM(instruction_budget=4).execute(program, capabilities=set())
        self.assertEqual(result.stack, (0xFFFF,))

    def test_syscall_outputs_do_not_leak_between_executions(self):
        host = SyscallHost(rcc=(0x3, 0x0, 0x0))
        vm = BytecodeVM(instruction_budget=64, host=host)
        emitting = assemble_text(RCC_MODULE.read_text()).bytecode
        halt_only = assemble_text("HALT\n").bytecode

        first = vm.execute(emitting, capabilities={CAP_RCC_READ, CAP_OUTPUT})
        self.assertNotEqual(first.outputs, ())

        second = vm.execute(halt_only, capabilities=set())
        self.assertEqual(second.outputs, ())
        self.assertEqual(host.outputs, [])


if __name__ == "__main__":
    unittest.main()
