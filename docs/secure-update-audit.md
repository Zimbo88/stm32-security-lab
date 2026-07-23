# Secure Update Chain Audit

Datum: 2026-07-23

## Scope

Gepruefter End-to-End-Pfad:

```text
stm32ctl
-> UART framing
-> protocol parser
-> update service
-> streaming installer
-> flash backend
-> boot metadata
-> candidate verification
-> candidate boot
-> confirmation / rollback
```

Der Fokus lag auf Speicherfehlern, Integer Overflows, ungueltigen
Zustandsuebergaengen, Parser-Desynchronisation, Replay/Duplicate-Faellen,
Retry-Semantik, unautorisierten Flashbereichen, Rollback-Protection,
Candidate-Freigabe, Stromausfallverhalten, Stackverbrauch, Bootloadergroesse
und nicht dokumentierten Annahmen.

## Summary

Ein konkreter Correctness-Fehler wurde gefunden und behoben:

- Wenn `FINISH_UPDATE` die Installation erfolgreich abgeschlossen und
  `CANDIDATE_READY` committed hatte, aber das abschliessende ACK/Status-Frame
  wegen UART-I/O-Fehler nicht gesendet werden konnte, kehrte
  `update_service_run()` vorher in den normalen Bootpfad zurueck. Der Candidate
  war dabei weiterhin verifiziert, aber der zugesagte kontrollierte Reset nach
  erfolgreichem Update wurde nicht strikt ausgefuehrt.
- `update_service_run()` loest nun auch nach einem Writer-/I/O-Fehler einen
  Reset aus, wenn der Protokollzustand bereits `FINISHED` oder
  `RESET_REQUESTED` ist.
- Ein Regressionstest simuliert einen Fehler beim Header-Write des finalen
  `FINISH_UPDATE`-ACKs und prueft, dass `CANDIDATE_READY` committed bleibt und
  der Reset angefordert wird.

Keine nachweisbare Moeglichkeit wurde gefunden, ueber den Updatepfad den
aktiven Slot, den Bootloaderbereich oder Recovery zu ueberschreiben.

## Reviewed Behavior

### stm32ctl

- Frames werden little-endian kodiert, auf 1024 Byte Payload begrenzt und mit
  CRC32 gesichert.
- Das Hosttool validiert lokale Updatepakete mit dem oeffentlichen Key und dem
  vorhandenen `tools/update_package.py`-Pfad, signiert aber nicht selbst.
- Nicht-idempotente Update-Kommandos werden nicht automatisch wiederholt.
  Dadurch wird ein verlorenes `WRITE_BLOCK`-ACK nicht durch ein doppeltes
  Flash-Schreiben kompensiert.
- Verlorene ACKs bei idempotenten Kommandos koennen fail-safe als
  Sequenzfehler enden; das ist eine Verfuegbarkeitsgrenze, keine beobachtete
  Integritaetsverletzung.

### UART Framing And Parser

- Der Parser arbeitet inkrementell, synchronisiert nur auf `SUPD` und lehnt
  Oversize-Payloads vor dem Schreiben in den Frame-Puffer ab.
- CRC, Version und Payloadlaenge werden vor der Kommandoausfuehrung geprueft.
- Frames mit falscher Version, CRC oder Oversize verbrauchen keine
  Sequenznummer. Gueltig dekodierte, aber logisch abgelehnte Kommandos
  verbrauchen eine Sequenznummer.
- Tests decken Byte-fuer-Byte-Eingang, fragmentierte Frames, mehrere Frames,
  Muell vor Magic, falsche Version, Oversize, CRC-Fehler, unbekannte Kommandos,
  doppelte und uebersprungene Sequenzen, Mid-Frame-Timeouts und zufaellige
  Eingaben ab.

### Update Service

- Das Einstiegfenster ist zeitlich und byte-maessig begrenzt. UART-Rauschen
  blockiert den Bootpfad nicht dauerhaft.
- Nur ein gueltiger binaerer `HELLO` mit Sequenz 0 fuehrt in den Update-Modus.
- Bei unvollstaendigem oder fehlgeschlagenem Update wird der gestartete
  Installer abgebrochen, soweit ein sicherer Abort moeglich ist.
- Nach der Korrektur fuehrt ein bereits erfolgreich abgeschlossenes Update auch
  bei finalem ACK-I/O-Fehler zum kontrollierten Reset.

### Streaming Installer

- Das vollstaendige Paket muss nicht im RAM liegen. Manifest/Header wird vor
  dem Loeschen des Candidate-Slots geprueft; Payload wird blockweise gehasht
  und programmiert.
- Der Installer bestimmt den inaktiven Slot aus bestaetigten Metadata und
  lehnt Pakete fuer den aktiven Slot ab.
- Rollback-Protection greift vor `WRITING` und vor dem Erase:
  `manifest.image_version` muss groesser als die bestaetigte Version in den
  Metadata sein.
- Payload-Offsets muessen exakt monoton sein. Uebersprungene, doppelte,
  ueberlappende oder zusaetzliche Daten setzen die Session in einen
  Fehlerzustand.
- `CANDIDATE_READY` wird erst nach Streaming-SHA-512, Flash-Readback-Hash,
  installierter Signatur-/Payload-Verifikation und Header-/Padding-Abgleich
  committed.
- `signed_image_verify_update_slot_buffer()` verwendet die uebergebene
  Kapazitaet nur als Obergrenze und hasht exakt `manifest.image_size`.

### Flash Backend And Metadata

- Der Installer initialisiert eine eingeschraenkte Flash-Instanz mit nur drei
  Schreibregionen: Metadata A, Metadata B und Candidate-Slot.
- `boot_flash_program_aligned()` prueft Adresse, Laenge, Alignment,
  erlaubte Schreibregionen und Readback.
- Metadata nutzt zwei Kopien, CRC und Commit-Marker. Torn writes bleiben ohne
  gueltigen Commit-Marker ungueltig.
- `WRITING` und `REJECTED_INVALID` booten den Candidate nicht. Nach Reset
  waehrend Begin/Erase/Write/Finish faellt die Slot-Auswahl auf den bestaetigten
  aktiven Slot zurueck, sofern dieser verifizierbar ist.

### Candidate Boot, Confirmation And Rollback

- `CANDIDATE_READY` wird beim Boot erneut verifiziert, bevor
  `PENDING_TRIAL` committed wird.
- Trial-Boots dekrementieren den Versuchscounter. Ohne App-Confirmation wird
  nach den konfigurierten Versuchen auf den bestaetigten aktiven Slot
  zurueckgefallen und der Candidate als ungueltig markiert.
- Die App-Confirmation ist nur aus `PENDING_TRIAL` fuer den laufenden
  Candidate-Slot gueltig und macht diesen Slot danach `CONFIRMED`.

## Finding Fixed

### Final ACK I/O Error After Successful Update

Risiko: Nach erfolgreichem `update_installer_finish()` war
`CANDIDATE_READY` bereits committed. Schlug danach das Senden des finalen
ACK/Status-Frames fehl, behandelte `update_service_run()` dies wie einen
allgemeinen Protokollfehler und kehrte in den normalen Bootpfad zurueck.

Auswirkung: Der Candidate konnte im anschliessenden Bootpfad weiterhin nur nach
erneuter Verifikation gebootet werden. Die Integritaet des Flashinhalts wurde
nicht verletzt. Das Verhalten wich aber vom Update-Service-Vertrag ab, nach
erfolgreichem Update einen kontrollierten Systemreset auszuloesen.

Korrektur:

- `firmware/exp045_bootloader_v2/src/update_service.c`: Bei
  `protocol_status != OK` wird jetzt zuerst auf `FINISHED` und
  `RESET_REQUESTED` geprueft. In diesen Zustaenden wird `request_reset()`
  ausgefuehrt.
- `tests/update_protocol/test_update_protocol.c`: Neuer Regressionstest
  `test_update_service_final_ack_io_error_still_resets()`.

## Remaining Assumptions And Limits

- UART-Update ist transportintegritaetsgesichert, aber nicht interaktiv
  authentisiert. Die Autorisierung liegt in der Ed25519-Signatur des Pakets und
  der Rollback-Policy, nicht in einer Host-Identitaet.
- Memory-mapped internal Flash muss waehrend Hash, Signaturpruefung und
  Sprungvorbereitung konsistent lesbar sein. Das Target-Backend invalidiert die
  Daten-Cache-Konfiguration um Flash-Operationen herum; ein kompletter
  Re-Hash unmittelbar vor `signed_image_jump()` erfolgt nicht.
- Nach Slot-Selection wird vor dem Sprung nur der vorbereitete
  Jump-Kontext gegen die Vector Table im Flash abgeglichen. Es wird angenommen,
  dass der Bootloader in dieser Phase keine weiteren Flash-Schreibpfade
  aktiviert.
- Verlorene ACKs fuer idempotente Host-Kommandos koennen als Sequenz-NACK
  sichtbar werden. Das Hosttool faellt dann ab, statt automatisch eine
  Resynchronisation mit Seiteneffekten zu versuchen.
- Das physische Update-GPIO ist weiterhin nicht gewaehlt; der Einstieg basiert
  auf dem begrenzten Binary-HELLO-Fenster.
- Es wurden keine dedizierten AFL/Hypothesis/QuickCheck-Fuzz-Targets im
  Repository gefunden. Vorhanden sind deterministische Parser- und
  Random-Input-Tests.

## Verification Run

Alle folgenden Befehle liefen erfolgreich:

```text
make -C tests/update_protocol clean test SANITIZE=1
make -C tests/update_storage clean test SANITIZE=1
make -C tests/uart clean test SANITIZE=1
make -C tests/diagnostic_console clean test SANITIZE=1
make -C tests/host_verifier clean test SANITIZE=1
make -C tools clean test SANITIZE=1
make -C tests/rsm_core clean test
python3 -m pytest
.venv-hil/bin/ruff check .
.venv-hil/bin/mypy .
cd tools/secure_boot_hil && ../../.venv-hil/bin/ruff check .
cd tools/secure_boot_hil && ../../.venv-hil/bin/mypy secure_boot_hil host_tests
make -C firmware/exp045_bootloader_v2 clean report
bash audit/run_repository_audit.sh
git diff --check
rg -n "installed_image_buffer|installed_image_buffer_size" . -S
```

Ergebnisse:

- Pytest: `157 passed in 39.27s`
- Ruff: `All checks passed`
- Mypy: `Success: no issues found in 79 source files`
- `secure_boot_hil` Ruff/Mypy: ohne Befund
- Bootloader-Binaergroesse: `29824 / 32768` Bytes
- Freier Bootloaderbereich: `2944` Bytes
- ELF: `.text 29560`, `.ramfunc 200`, `.bss 4040`, `dec 33864`
- `rg installed_image_buffer`: keine Treffer

Hoechste Stack-Usage-Werte aus `-fstack-usage`:

```text
2256 crypto_argon2
1088 crypto_eddsa_check_equation
848  update_installer_install
312  hash_installed_payload
288  update_package_verify_header_for_slot
280  boot_metadata_recover_from_flash
232  update_mode_poll_and_process
216  signed_image_verify_buffer_with_policy
192  boot_metadata_commit
176  signed_image_prepare_update_slot_buffer
```

## Files Changed By This Audit

```text
firmware/exp045_bootloader_v2/src/update_service.c
tests/update_protocol/test_update_protocol.c
docs/secure-update-audit.md
```
