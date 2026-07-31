# Abschlussbericht: RDP2-Vorbereitung

Stand: 2026-07-31, Branch `research/rdp2-final-preparation`.

RDP Level 2 wurde nicht aktiviert. Es wurden keine Option Bytes geschrieben,
kein Programmer-Befehl ausgeführt und keine Hardwareoperation mit dauerhaftem
Sperrrisiko durchgeführt. Bestehende Tags und Releases blieben unverändert.

## Implementiert

- Synthetische Marker für Bootloader-Flash, Slot A, Slot B und SRAM mit
  dokumentierten ELF-Symbolen, Positionen, Längen und SHA-256-Hashwerten.
- Konfigurationsmarker als reine Repository-Referenz. Ein persistenter Marker
  im Boot-Metadatenbereich wurde bewusst nicht eingebaut, weil das bestehende
  kanonische Padding und die Sektor-Commitlogik dadurch verletzt würden.
- `tools/rdp2_marker.py` für ELF-/Binärprüfung und die Suche vollständiger oder
  partieller Marker in späteren Dumps.
- `tools/rdp2_baseline.py` für lokale, ignorierte Vor-RDP2-Baselines mit
  Bootloader-, Slot-, Paket-, Layout-, Symbol-, Manifest- und Hashartefakten.
  Das Werkzeug kopiert oder druckt keine privaten Signierschlüssel und besitzt
  keine Target-, Programmer- oder Option-Byte-Funktion.
- Debugger-unabhängiges UART-Testverfahren und reproduzierbare
  Power-Loss-Testkampagne einschließlich Zustands-, Slot-, Metadaten-, UART-
  und Recovery-Erwartungen.
- Manuelle RDP2-Checkliste sowie Dokumentation der nach RDP2 erforderlichen
  Schlüssel, Updatebefehle, UART-Grenzen und der irreversiblen Stage-0-Grenze.
- Hosttest für Marker, UART-only-Testanforderungen, ignorierte Baseline und das
  Fehlen eines automatischen RDP2-/Option-Byte-Schreibziels.

Die bestehende Secure-Boot-, Dual-Slot-, Update- und RSM-Architektur wurde
nicht erweitert. Der bestehende Installer bestimmt den inaktiven Slot aus den
bestätigten Metadaten, prüft Paket, Zielvektor, Version und Signatur vor dem
Erase, programmiert mit Readback und setzt `CANDIDATE_READY` erst nach der
vollständigen Verifikation.

## Geänderte Dateien

- `.gitignore`: ignoriert `baseline/rdp2-final/`.
- `config/rdp2_research_markers.json`: zentrale synthetische Markerdefinition.
- `firmware/exp045_bootloader_v2/linker.ld`: Markerabschnitt und ELF-Symbole
  im Stage-0-Flash.
- `firmware/exp045_bootloader_v2/src/research_markers.c`: Bootloadermarker.
- `firmware/exp066_research_platform_core/Makefile`: Slot-A/Slot-B-Marker-
  Präprozessorumschaltung.
- `firmware/exp066_research_platform_core/linker.ld`: Flash- und SRAM-
  Markerabschnitte sowie ELF-Symbole.
- `firmware/exp066_research_platform_core/src/platform.c`: sichere SRAM-
  Markerinitialisierung vor der normalen UART-Initialisierung.
- `firmware/exp066_research_platform_core/src/research_markers.c`: Slot- und
  SRAM-Marker.
- `firmware/exp066_research_platform_core/src/research_markers.h`: deklarierte
  Markerinitialisierung.
- `tools/rdp2_marker.py`: Markerinspektion, Hashes und Dumpsuche.
- `tools/rdp2_baseline.py`: Baseline-Erzeugung und Verifikation.
- `tests/test_rdp2_research_preparation.py`: debuggerunabhängige Hostprüfungen
  und Safety-Assertions.
- `docs/rdp2-preparation-audit.md`: Bestandsaufnahme und Evidenzstatus.
- `docs/rdp2-uart-only-hardware-test.md`: UART-only-Hardwareverfahren.
- `docs/rdp2-power-loss-campaign.md`: Power-Loss-Matrix und Recoveryverfahren.
- `docs/rdp2-provisioning-checklist.md`: manuelle RDP2-Checkliste ohne
  Aktivierungsbefehl.
- `docs/post-rdp2-update-and-key-requirements.md`: Schlüssel- und
  Updatefähigkeit nach RDP2.
- `docs/rdp2-uart-behavior.md`: USART1-, Reset-, Clock- und Debuggergrenzen.
- `docs/rdp2-preparation-report.md`: dieser Abschlussbericht.

## Testergebnisse

Erfolgreich ausgeführt:

- `python3 -m pytest -q tests` → **142 passed**.
- `make -C tests/host_verifier clean test` → `host verifier tests passed`.
- `make -C tests/update_storage clean test` → `update storage tests passed`.
- `make -C tests/update_protocol clean test` → `update protocol tests passed`.
- `make -C tests/uart clean test` → `uart tests passed`.
- `make -C tests/rsm_core clean test` → Exitcode 0.
- `make -C tests/diagnostic_console clean test` → Exitcode 0.
- `.venv-hil/bin/ruff check tools/rdp2_marker.py tools/rdp2_baseline.py
  tests/test_rdp2_research_preparation.py` → **All checks passed**.
- `python3 tools/check_deterministic_build.py` → **Deterministic build
  comparison passed**.
- `python3 tools/rdp2_marker.py inspect` → alle fünf Marker verifiziert.
- `python3 tools/rdp2_baseline.py verify` → Baseline mit **17 Artefakten**
  verifiziert.
- `python3 tools/update_package.py verify` für Slot A und Slot B mit dem
  eingebetteten Public-Key → beide Exitcode 0.
- `git diff --check` → sauber.

Die geprüfte Baseline enthält Bootloader- und Slot-Hashes, Layout, ELF-
Symboltabellen, Markerpositionen, Versionen, Public-Key-Fingerprint,
Toolchaininformation, Git-Commit, erwartete UART-Ausgabe und erwartete
Metadaten. Private Schlüssel wurden weder kopiert noch ausgegeben.

## Hardwarevalidierung

Tatsächlich ausgeführt:

- Nur ein nichtmutierender UART-Handschlag:
  `PYTHONPATH=tools python3 -m stm32ctl --port /dev/ttyUSBx --timeout 3
  --retries 0 --json info` → Exitcode 4, Timeout beim Warten auf UART-Daten.
- Keine Update-, Reset-, Erase-, Program-, Option-Byte- oder RDP-Aktion auf
  Hardware.

Nur vorbereitet:

- A → B, B → A, falscher Slot, falsche Signatur, Rollback, unvollständiges
  Paket, Updateabbruch und debuggerfreie Recovery sind in
  `docs/rdp2-uart-only-hardware-test.md` ausführbar beschrieben.
- Alle geforderten Stromunterbrechungsphasen sind in
  `docs/rdp2-power-loss-campaign.md` mit erwarteten Zuständen und Recovery
  beschrieben.

Noch offen:

- Reale UART-only-Tests mit separatem USB-UART auf einem eindeutig verfügbaren
  Referenzboard.
- Reale A → B- und B → A-Updates ohne SWD, JTAG, ST-Link-VCP oder ROM-
  Bootloader.
- Physische Power-Loss-Kampagne über alle dokumentierten Commit- und
  Trial-Boot-Grenzen.
- Endgültige Bestätigung der Boardkennung, Option-Byte-Ausgangsdokumentation
  und Backup-/Git-Bundle-Checks auf dem entbehrlichen Zielgerät.
- Jeglicher Nachweis eines tatsächlich aktivierten RDP2-Chips; RDP2 bleibt
  absichtlich deaktiviert.

## Verbleibende RDP2-Blocker

1. Die vollständige debuggerfreie Hardwarekette ist wegen des UART-Timeouts
   nicht real nachgewiesen.
2. Die physische Power-Loss-Kampagne wurde nicht durchgeführt.
3. Ein fehlerfreier Stage-0-Start und eine funktionierende USART1-/Clock-/NRST-
   Kette unter den finalen elektrischen Bedingungen sind offen.
4. Das Verfahren setzt eine vorab vorhandene, gültige bestätigte Firmware und
   Metadatenprovisionierung voraus; ein leerer Chip kann nicht ausschließlich
   über den normalen Slot-Updatepfad hergestellt werden.
5. RDP2 selbst ist absichtlich nicht getestet und darf erst nach separater
   manueller Hardwarefreigabe betrachtet werden.

## Irreversible Risiken

- Ein Fehler oder eine Beschädigung des Stage-0-Bootloaders ist über den
  vorhandenen UART-Slotpfad nicht aktualisierbar.
- Der Verlust des privaten Ed25519-Signierschlüssels beendet die praktische
  Updatefähigkeit für den eingebetteten Public Key.
- Eine fehlerhafte Clock- oder USART1-Initialisierung, ein falsches Pinout,
  Baudrate- oder Reset-Timing kann den einzigen vorgesehenen Bedienweg
  abschneiden.
- Beschädigte Metadatenkopien, ein fehlerhafter Flash-Erase oder ein Fehler in
  der Commitreihenfolge können zu keinem bootbaren Slot führen.
- Nach RDP2 ist keine Reparatur über SWD/JTAG/ST-Link, kein STM32-ROM-
  Bootloader-Recoveryweg und kein normales Reflashing vorausgesetzt oder
  verfügbar.
- RDP2 kann auf dem Ziel nicht zurückgenommen werden.

## Gitstand

Die Änderungen liegen auf `research/rdp2-final-preparation` in logisch
getrennten Commits. Es wurde nichts gepusht und kein Release erstellt. Die
vorhandenen Tags, einschließlich `v1.0.2`, wurden nicht verändert.

## Empfehlung

SOFTWARE READY FOR FINAL HARDWARE VALIDATION – RDP2 NOT YET ENABLED
