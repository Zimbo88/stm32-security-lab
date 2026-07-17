from __future__ import annotations

import hashlib
import struct
import subprocess
import sys
import unittest
import zlib
from pathlib import Path
from tempfile import TemporaryDirectory

sys.path.insert(0, str(Path(__file__).parents[1] / "tools"))

from bytecode_asm import assemble_text
from bytecode_vm import CAP_OUTPUT, CAP_RCC_READ
from module_format import TYPE_BYTECODE, build_package
from module_install import (
    INSTALL_RECORD_BODY_SIZE,
    INSTALL_RECORD_SIZE,
    SLOT_B,
    SLOT_NONE,
    CatalogRecord,
    InstallError,
    InstallState,
    ModuleInstaller,
    PowerLoss,
    SimulatedFlash,
    encode_record,
    recover_catalog,
)
from nacl.signing import SigningKey


ROOT = Path(__file__).parents[1]
MODULE_ID = 0x7001


def _package(key: SigningKey, version: int, *, module_id: int = MODULE_ID, min_platform: int = 1) -> bytes:
    bytecode = assemble_text("HALT\n").bytecode
    return build_package(
        bytecode,
        module_id=module_id,
        version=version,
        seed=bytes(key),
        capabilities=[CAP_OUTPUT],
        module_type=TYPE_BYTECODE,
        min_platform=min_platform,
        abi_version=1,
    )


def _installer(
    flash: SimulatedFlash,
    key: SigningKey,
    *,
    firmware_public_key: bytes | None = None,
    failure_threshold: int = 2,
) -> ModuleInstaller:
    return ModuleInstaller(
        flash,
        module_public_key=bytes(key.verify_key),
        firmware_public_key=firmware_public_key,
        failure_threshold=failure_threshold,
    )


def _confirmed_flash(key: SigningKey, version: int = 1) -> tuple[SimulatedFlash, bytes]:
    flash = SimulatedFlash()
    installer = _installer(flash, key)
    package = _package(key, version)
    installer.install(package)
    installer.activate(MODULE_ID)
    installer.confirm(MODULE_ID)
    return flash, package


class Exp070InstallTests(unittest.TestCase):
    def test_install_activate_confirm_state_machine(self):
        key = SigningKey.generate()
        flash = SimulatedFlash()
        installer = _installer(flash, key)
        package = _package(key, 1)

        installer.install(package)
        self.assertEqual(installer.inspect(MODULE_ID).state, InstallState.VERIFIED)
        installer.verify(MODULE_ID)

        installer.activate(MODULE_ID)
        self.assertEqual(installer.inspect(MODULE_ID).state, InstallState.ACTIVE)
        self.assertIsNone(installer.current_confirmed_package(MODULE_ID))

        with self.assertRaisesRegex(InstallError, "installation in progress"):
            installer.install(_package(key, 2))

        installer.confirm(MODULE_ID)
        self.assertEqual(installer.inspect(MODULE_ID).state, InstallState.CONFIRMED)
        self.assertEqual(installer.current_confirmed_package(MODULE_ID), package)

    def test_interrupted_install_keeps_confirmed_module_available(self):
        key = SigningKey.generate()
        base_flash, confirmed_package = _confirmed_flash(key, 1)
        upgrade = _package(key, 2)

        for fail_after in (1, 2, 3):
            flash = base_flash.clone()
            flash.set_power_loss_after(fail_after)
            installer = _installer(flash, key)
            with self.assertRaises(PowerLoss):
                installer.install(upgrade)
            recovered = _installer(flash, key)
            self.assertEqual(recovered.current_confirmed_package(MODULE_ID), confirmed_package)

    def test_interrupted_activation_confirmation_and_rollback_recover_deterministically(self):
        key = SigningKey.generate()
        base_flash, confirmed_package = _confirmed_flash(key, 1)
        upgrade = _package(key, 2)

        flash = base_flash.clone()
        installer = _installer(flash, key)
        installer.install(upgrade)
        for fail_after in (1, 2):
            trial = flash.clone()
            trial.set_power_loss_after(fail_after)
            with self.assertRaises(PowerLoss):
                _installer(trial, key).activate(MODULE_ID)
            self.assertEqual(_installer(trial, key).current_confirmed_package(MODULE_ID), confirmed_package)

        active_flash = flash.clone()
        active = _installer(active_flash, key)
        active.activate(MODULE_ID)
        trial = active_flash.clone()
        trial.set_power_loss_after(1)
        with self.assertRaises(PowerLoss):
            _installer(trial, key).confirm(MODULE_ID)
        self.assertIsNotNone(_installer(trial, key).current_confirmed_package(MODULE_ID))

        trial = active_flash.clone()
        trial.set_power_loss_after(1)
        with self.assertRaises(PowerLoss):
            _installer(trial, key).rollback(MODULE_ID)
        self.assertEqual(_installer(trial, key).current_confirmed_package(MODULE_ID), confirmed_package)

    def test_pending_failure_restores_confirmed_and_quarantines_after_threshold(self):
        key = SigningKey.generate()
        flash, confirmed_package = _confirmed_flash(key, 1)
        installer = _installer(flash, key, failure_threshold=2)
        installer.install(_package(key, 2))
        installer.activate(MODULE_ID)

        installer.record_initialization_result(MODULE_ID, success=False)
        self.assertEqual(installer.inspect(MODULE_ID).state, InstallState.REJECTED)
        self.assertEqual(installer.current_confirmed_package(MODULE_ID), confirmed_package)

        installer.activate(MODULE_ID)
        installer.record_initialization_result(MODULE_ID, success=False)
        self.assertEqual(installer.inspect(MODULE_ID).state, InstallState.QUARANTINED)
        self.assertEqual(installer.current_confirmed_package(MODULE_ID), confirmed_package)

    def test_corrupted_catalog_and_partial_package_are_ignored_safely(self):
        key = SigningKey.generate()
        flash, confirmed_package = _confirmed_flash(key, 1)
        installer = _installer(flash, key)
        installer.install(_package(key, 2))

        corrupt = bytearray(bytes(flash.catalog[-128:]))
        corrupt[20] ^= 0xFF
        flash.append_partial_catalog(bytes(corrupt), len(corrupt))
        view = recover_catalog(bytes(flash.catalog))
        self.assertGreaterEqual(len(view.records), 4)

        malformed = bytearray(
            encode_record(
                CatalogRecord(
                    1,
                    0xFFFF,
                    MODULE_ID,
                    2,
                    1,
                    InstallState.VERIFIED,
                    SLOT_NONE,
                    SLOT_B,
                    0,
                    0,
                    hashlib.sha256(bytes(key.verify_key)).digest()[:16],
                    b"\x5a" * 32,
                )
            )
        )
        struct.pack_into("<I", malformed, 28, 99)
        struct.pack_into("<I", malformed, INSTALL_RECORD_BODY_SIZE, zlib.crc32(malformed[:INSTALL_RECORD_BODY_SIZE]) & 0xFFFFFFFF)
        flash.append_partial_catalog(bytes(malformed), INSTALL_RECORD_SIZE)
        self.assertNotIn(0xFFFF, recover_catalog(bytes(flash.catalog)).latest_by_module)

        flash.write_partial_slot(MODULE_ID, SLOT_B, _package(key, 2), 24)
        with self.assertRaises(Exception):
            installer.verify(MODULE_ID)
        self.assertEqual(installer.current_confirmed_package(MODULE_ID), confirmed_package)

    def test_anti_rollback_signer_revocation_and_key_separation(self):
        key = SigningKey.generate()
        flash, _ = _confirmed_flash(key, 2)
        installer = _installer(flash, key)

        with self.assertRaisesRegex(InstallError, "rollback version"):
            installer.install(_package(key, 1))
        with self.assertRaisesRegex(InstallError, "rollback version"):
            installer.install(_package(key, 2))

        installer.revoke_signer(hashlib.sha256(bytes(key.verify_key)).digest()[:16])
        with self.assertRaisesRegex(InstallError, "revoked"):
            installer.install(_package(key, 3))

        same_key_flash = SimulatedFlash()
        same_key_installer = _installer(same_key_flash, key, firmware_public_key=bytes(key.verify_key))
        with self.assertRaisesRegex(InstallError, "separate"):
            same_key_installer.install(_package(key, 1))

    def test_candidate_management_and_remove(self):
        key = SigningKey.generate()
        flash, confirmed = _confirmed_flash(key, 1)
        installer = _installer(flash, key)
        installer.install(_package(key, 2))
        self.assertEqual(installer.inspect(MODULE_ID).state, InstallState.VERIFIED)

        installer.quarantine(MODULE_ID)
        self.assertEqual(installer.inspect(MODULE_ID).state, InstallState.QUARANTINED)
        installer.remove_candidate(MODULE_ID)
        self.assertEqual(installer.inspect(MODULE_ID).state, InstallState.CONFIRMED)
        self.assertEqual(installer.current_confirmed_package(MODULE_ID), confirmed)

    def test_cli_install_verify_activate_confirm_and_recovery(self):
        key = SigningKey.generate()
        package = _package(key, 1)
        with TemporaryDirectory() as tmpdir_name:
            tmpdir = Path(tmpdir_name)
            flash_path = tmpdir / "flash.json"
            package_path = tmpdir / "module.pkg"
            package_path.write_bytes(package)
            base = [
                sys.executable,
                str(ROOT / "tools" / "module_install.py"),
                "--flash",
                str(flash_path),
                "--module-public-key",
                bytes(key.verify_key).hex(),
            ]

            install = subprocess.run(
                base + ["install", str(package_path)],
                check=True,
                text=True,
                capture_output=True,
            )
            self.assertIn("installed candidate", install.stdout)

            verify = subprocess.run(base + ["verify", hex(MODULE_ID)], check=True, text=True, capture_output=True)
            self.assertIn("verified", verify.stdout)
            subprocess.run(base + ["activate", hex(MODULE_ID)], check=True, text=True, capture_output=True)
            subprocess.run(base + ["confirm", hex(MODULE_ID)], check=True, text=True, capture_output=True)

            listing = subprocess.run(base + ["list"], check=True, text=True, capture_output=True)
            self.assertIn("CONFIRMED", listing.stdout)
            recovery = subprocess.run(base + ["catalog-recovery"], check=True, text=True, capture_output=True)
            self.assertIn("modules=1", recovery.stdout)


if __name__ == "__main__":
    unittest.main()
