# EXP041 – Repository, Firmware and Recovery Inventory

## Zweck

Vollständige Offline-Bestandsaufnahme des bisherigen Projekts.

EXP041 führt keine Kommunikation mit dem STM32 durch.

## Zusammenfassung

- Firmware-Dateien: **299**
- Firmware-Projekte und Unterordner: **45**
- Recovery- und Baseline-Dateien: **13**
- Skriptdateien: **5**
- Experimentordner: **22**
- Sicherheitsrelevante Dateien: **93**

## Sicherheitsmodell für die nächste Phase

- Zielgerät STM32F429VET6: weiterhin ausschließlich Read-Only.
- Laborboard STM32F429IGT6: kontrollierte Firmware- und Schutzexperimente erlaubt.
- Kein RDP Level 2 auf dem weiterzuverwendenden Laborboard.
- Vor Schutzexperimenten müssen vollständige Recovery-Artefakte vorliegen.
- RDP1-Experimente benötigen einen getesteten Mass-Erase-Recovery-Pfad.
- Irreversible Konfigurationen nur auf ausdrücklich entbehrlicher Hardware.
- Der Wartungszugang muss authentifiziert und dokumentiert sein.
- Kein versteckter universeller Speicherauslese-Zugang.

## Vorhandene Recovery-Artefakte

- `baseline/exp024_recovery/full_flash_1MiB.bin`
- `baseline/exp024_recovery/option_registers.txt`
- `baseline/exp024_recovery/option_register_values.txt`
- `baseline/exp024_recovery/RECOVERY.md`
- `baseline/exp024_recovery/sector0_bootloader_16KiB.bin`
- `baseline/exp024_recovery/sector2_signed_image_16KiB.bin`
- `baseline/exp024_recovery/SHA256SUMS`
- `baseline/exp024_recovery.tar.gz`
- `baseline/exp024_recovery.tar.gz.sha256`
- `baseline/internal_flash_initial.bin`
- `baseline/internal_flash_initial.sha256`
- `baseline/pre_bootloader/internal_flash_before_exp013.bin`
- `baseline/pre_bootloader/internal_flash_before_exp013.sha256`

## Vorhandene Firmware-Projekte

- `firmware`
- `firmware/exp002_gpio_led`
- `firmware/exp002_gpio_led/build`
- `firmware/exp002_gpio_led/src`
- `firmware/exp003_uart`
- `firmware/exp003_uart/build`
- `firmware/exp003_uart/src`
- `firmware/exp004_register_explorer`
- `firmware/exp004_register_explorer/build`
- `firmware/exp004_register_explorer/src`
- `firmware/exp005_system_identity`
- `firmware/exp005_system_identity/build`
- `firmware/exp005_system_identity/src`
- `firmware/exp007_memory_map`
- `firmware/exp007_memory_map/build`
- `firmware/exp007_memory_map/src`
- `firmware/exp008_clock_explorer`
- `firmware/exp008_clock_explorer/build`
- `firmware/exp008_clock_explorer/src`
- `firmware/exp009_pll_84mhz`
- `firmware/exp009_pll_84mhz/build`
- `firmware/exp009_pll_84mhz/src`
- `firmware/exp010_mpu_fault`
- `firmware/exp010_mpu_fault/build`
- `firmware/exp010_mpu_fault/src`
- `firmware/exp013_application`
- `firmware/exp013_application/build`
- `firmware/exp013_application/src`
- `firmware/exp013_bootloader`
- `firmware/exp013_bootloader/build`
- `firmware/exp013_bootloader/src`
- `firmware/exp014_application_crc`
- `firmware/exp014_application_crc/build`
- `firmware/exp014_application_crc/src`
- `firmware/exp014_bootloader_crc`
- `firmware/exp014_bootloader_crc/build`
- `firmware/exp014_bootloader_crc/src`
- `firmware/exp019_signed_bootloader`
- `firmware/exp019_signed_bootloader/build`
- `firmware/exp019_signed_bootloader/src`
- `firmware/exp022_rollback_floor`
- `firmware/exp022_rollback_floor/build`
- `firmware/exp022_rollback_floor/src`
- `firmware/exp032_device_inventory`
- `firmware/exp032_device_inventory/src`

## Experimentverzeichnisse

- `exp011_option_bytes`
- `exp012_rom_bootloader`
- `exp023_protection_audit`
- `exp024_recovery`
- `exp025_option_byte_analysis`
- `exp026_sector0_wrp`
- `exp027a_rdp_planning`
- `exp027b_rdp1`
- `exp028_final_audit`
- `exp029_boot_timing`
- `exp030_reset_fault`
- `exp031_final_validation`
- `exp032_device_inventory`
- `exp033_flash_option_analysis`
- `exp034_memory_access`
- `exp035_extended_access`
- `exp036_coresight_inventory`
- `exp037_reset_state_comparison`
- `exp038_access_consistency`
- `exp039_coresight_component_ids`
- `exp040_consolidated_report`
- `exp041_repository_inventory`

## Nächste technische Phase

EXP042 prüft den vorhandenen Bootloader, das Speicherlayout und den Recovery-Pfad offline.
