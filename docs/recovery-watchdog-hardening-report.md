# Recovery- und Watchdog-Hardening – Abschlussbericht

Datum: 2026-07-31  
Branch: `feature/recovery-watchdog-hardening`  
Ausgangsbasis: `research/rdp2-final-preparation`  
RDP: vor und nach den Tests unverändert Level 0

## Ausgangszustand

Die Änderungen aus `research/rdp2-final-preparation` waren bereits in der
Arbeitsbasis enthalten. Der Branch enthielt die sechs RDP2-Vorbereitungs-
Commits für synthetische Marker, Baseline-Artefakte, debuggerunabhängige
Update-Tests, Power-Loss-Dokumentation, Provisionierungs-Checkliste und
Schlüssel-/Update-Dokumentation. Diese Commits wurden nicht verändert und
mussten nicht erneut gemergt werden.

Vor diesem Auftrag waren Dual-Slot-Secure-Boot, redundante Boot-Metadaten,
signierte UART-Updates, Hash-/Readback-Prüfung, Versionsschutz und ein
grundsätzliches Trial-/Confirmation-Modell vorhanden. Die kritischen Lücken
waren der fehlende normalisierte Reset-Cause-Pfad, fehlende IWDG-Integration,
kein automatischer UART-Recovery-Dienst bei einer Boot-Policy-Störung und eine
unvollständige Behandlung eines bereits abgelehnten Kandidaten als erneut
beschreibbarer inaktiver Slot. Der bisherige Recovery-Fallback hielt in einem
Haltzustand an. Das Health-Gate war vorhanden, verlangte aber nicht alle für
einen belastbaren Trial-Boot erforderlichen Voraussetzungen.

Die Hardware wurde ausschließlich mit RDP0 betrieben. Es wurden keine
Option-Bytes, keine Write-Protection und kein RDP geändert. Die vorhandene
Hardwareidentifikation lautet:

| Merkmal | Befund |
|---|---|
| MCU | STM32F42x/F43x, Chip-ID `0x419` |
| Flash/SRAM | 1 MiB / 256 KiB |
| Boardzugang | ST-Link V2J37S7 für Entwicklungs- und Diagnosephasen |
| UART | `/dev/ttyUSB0`, FT232R, für USART1 |
| Option-Byte-Lesung | `0x0fffaaed`, RDP-Byte `0xAA` = RDP0 |
| NRST | Laut ST-Link nicht angeschlossen; Reset wurde daher über AIRCR ausgelöst |

Die vollständige read-only Identifikation und das Backup liegen außerhalb des
Git-Repositories unter
`hil-results/recovery-watchdog/2026-07-31/`. Das Backup enthält lokale
Laborartefakte und ist durch `.gitignore` ausgeschlossen.

## Implementierte Änderungen

### Boot-Recovery und Slot-Policy

- Die Zustände `EMPTY`, `WRITING`, `CANDIDATE_READY`, `TRIAL`, `CONFIRMED`,
  `REJECTED` und `INVALID` sind in
  [`docs/boot-recovery-state-machine.md`](boot-recovery-state-machine.md)
  definiert. Die Implementierung verwendet weiterhin
  `PENDING_TRIAL` als persistente Repräsentation von `TRIAL`.
- Ein Trial-Boot verbraucht persistent eine von drei Versuchen. Ein
  unbestätigter Reset wird mit der normalisierten Resetursache im
  Metadaten-Resultat gespeichert. Bei Erreichen der Grenze wird der Kandidat
  als `REJECTED_INVALID` behandelt und der bestätigte Slot verifiziert und
  gestartet.
- Ein fehlgeschlagener Kandidat beschädigt den bestätigten Slot nicht. Ein
  `REJECTED_INVALID`-Kandidat kann anschließend als gegenüberliegender
  inaktiver Slot erneut signiert aktualisiert werden.
- Bei fehlender oder nicht vertrauenswürdig auflösbarer Bootbasis wechselt
  Stage-0 in den bestehenden signierten UART-Dienst, statt unbedient zu
  halten. Der Recovery-Bootstrap ist auf ein signiertes Slot-A-Paket, die
  beiden Metadatensektoren und den normalen Trial-/Confirmation-Pfad begrenzt.
- Die vorhandene Metadatenredundanz bleibt fail-closed: ungültige CRCs,
  widersprüchliche gültige Generationen, reservierte Statuswerte und
  Sequenzüberlauf werden nicht stillschweigend geraten oder gewrappt.

### Resetursachen und IWDG

- RCC-Resetflags werden vor dem Löschen normalisiert und als
  `BOOT_RESET cause=<NAME> raw=0xXXXXXXXX` ausgegeben. Die Priorität lautet
  IWDG, WWDG, Software, Brownout, Pin, Power-On, Low-Power, unbekannt.
- Der STM32F429-IWDG wird softwareseitig mit LSI, Prescaler `/64` und Reload
  `2000` aktiviert. Das ergibt nominal etwa 4002 ms; bei der dokumentierten
  LSI-Spanne von 17–47 kHz etwa 2724–7533 ms. Es wird keine IWDG-Option-Byte-
  Einstellung verwendet.
- Stage-0 frischt den bereits laufenden IWDG nur an begrenzten Fortschritts-
  punkten während UART, Flash-Erase/-Program und Hashprüfung. Unbegrenzte
  Leerlaufschleifen werden nicht durch Refresh am Leben gehalten.
- Die Anwendung setzt den IWDG nach UART-, Plattform- und RSM-Initialisierung
  in Betrieb und bedient ihn am Ende einer begrenzten Idle-Einheit.
- Ein IWDG-Reset beeinflusst einen Trial-Slot, ein bestätigter Slot bleibt
  bestätigt und erhält nur Diagnose-/RSM-Einträge.

### Health-Gate und Testfirmware

Das Health-Gate verlangt nun Plattformstart, UART-Diagnose, initialisierten
RSM, aktiven IWDG, einen stabilen Ausführungspunkt und die Anwendungs-
Gesundheitsbedingung. Die Confirmation wird als strukturierte Ausgabe
`HEALTH_GATE` und `SLOT_CONFIRMATION` sichtbar.

Die ausschließlich explizit aktivierbaren Szenarien sind:

`trial_success`, `trial_no_confirm`, `trial_hardfault`,
`trial_watchdog_hang`, `trial_software_reset`, `trial_health_fail`,
`trial_invalid_vector` und `trial_delayed_confirm`.

Sie sind über `TEST_SCENARIO=...` buildbar, nicht Teil des normalen Release-
Defaults und tragen eine eindeutige UART-Ausgabe.

### Recovery und Protokollierung

- Die bestehende signierte UART-Updatepipeline bleibt auch Recoverypfad:
  Framing, CRC, Sequenz, Zielslot, Adressbereich, Versionspolicy, Public-Key,
  Ed25519-Signatur, Payload-Hash, Readback und Metadatencommit werden nicht
  umgangen.
- `stm32ctl recovery` ist ein explizites Bedienlabel für denselben signierten
  Updatepfad und verlangt für den Bootstrap Slot A. Der Stage-0-Konsolenbefehl
  `recovery` führt ebenfalls nur in diesen signierten Modus.
- Recovery erlaubt weder Stage-0-Schreiben noch allgemeines Memory Peek/Poke,
  unsignierte Images oder den ROM-Bootloader als Voraussetzung.
- RSM-Ereignisse für Resetursache, Watchdog, Health-Gate und Recovery-Policy
  wurden ergänzt. Firmwareinhalte und Schlüssel werden nicht protokolliert.

## Geänderte Dateien

Die Änderungen sind logisch nach Funktion gruppiert:

- `firmware/exp045_bootloader_v2/src/boot_{metadata,policy,sequence,slot_selection}.*`:
  Trial-Zähler, Fallback, Metadaten-Recovery und Auswahlregeln.
- `firmware/exp045_bootloader_v2/src/reset_cause.*`:
  RCC-Normalisierung, Priorität und maschinenlesbare Ausgabe.
- `firmware/exp045_bootloader_v2/src/boot_watchdog.*` sowie
  `update_service.*`, `update_installer.*` und `update_mode.*`:
  softwarebasierte Stage-0-IWDG-Bedienung und signierter Recoverydienst.
- `firmware/exp045_bootloader_v2/src/diagnostic_console.*` und `main.c`:
  expliziter Recovery-Einstieg und Dispatch.
- `firmware/exp066_research_platform_core/Makefile` und `src/`:
  IWDG, Health-Gate-Voraussetzungen, Reset-/RSM-Diagnose und kontrollierte
  Testfirmaturen.
- `tests/reset_cause/`, `tests/test_reset_cause.py`,
  `tests/update_storage/test_update_storage.c` und
  `tests/diagnostic_console/test_diagnostic_console.c`:
  Reset-, Trial-, Recovery-, Fallback- und Konsolentests.
- `tools/stm32ctl/cli.py`:
  explizites signiertes `recovery`-Kommando.
- `README.md`, `docs/platform-feature-matrix.md` und die acht neuen
  Recovery-/Watchdog-Dokumente:
  öffentliche Architektur-, Test- und Evidenzdokumentation.

Es wurde kein automatisches RDP2-, Option-Byte- oder Write-Protection-
Kommando hinzugefügt. Es gibt kein `rdp2`-Make-Ziel und keinen CI-Schreib-
Schritt für Option Bytes.

## Hosttests

Ausgeführt wurden unter anderem:

```text
make -C firmware/exp045_bootloader_v2 clean all
make -C firmware/exp066_research_platform_core SLOT=b TEST_SCENARIO=normal clean all
make -C firmware/exp066_research_platform_core SLOT=b TEST_SCENARIO=<scenario> test-scenario
make -C tests/update_storage clean test
make -C tests/reset_cause clean test
make -C tests/update_protocol clean test
make -C tests/diagnostic_console clean test
make -C tests/host_verifier clean test
make -C tests/uart clean test
make -C tests/rsm_core clean test
for d in host_verifier update_storage update_protocol uart diagnostic_console; do
  make -C tests/$d clean test SANITIZE=1 || exit 1
done
PYTHONDONTWRITEBYTECODE=1 python3 -m pytest -q -p no:cacheprovider tests
```

Ergebnisse:

- Bootloader-Build: erfolgreich, 30836 Bytes von 32768 Bytes.
- Normaler finaler Slot-A-/Slot-B-Build: erfolgreich; Slot B ELF etwa
  20.6 KiB Text, `-Wall -Wextra -Werror`.
- Alle acht Testfirmware-Szenarien: erfolgreich gebaut.
- C-Hosttests einschließlich Sanitizern: erfolgreich.
- Python-Suite: `143 passed in 43.08s`.
- `tests/update_storage` deckt Recovery-Bootstrap, IWDG-Trialresultat,
  Kandidaten-Fallback und erneutes Aktualisieren des abgelehnten inaktiven
  Slots ab.
- Reset-Cause-Hosttest: erfolgreich; kombinierte RCC-Flags werden nach der
  dokumentierten Priorität bewertet.

Die Metadatenkorruptionsmatrix ist als Policy und Hosttestgrundlage vorhanden;
die vollständige physische Provisionierung jeder Matrixzeile ist kein Ersatz
für den noch offenen Hardwaretest und wurde nicht als vollständig hardware-
validiert markiert.

## Qualitätsprüfungen

Zusätzlich zu den funktionalen Tests wurden folgende Prüfungen ausgeführt:

```text
python3 tools/check_deterministic_build.py
python3 tools/rdp2_marker.py inspect --output /tmp/stm32-marker-report.json
python3 tools/rdp2_baseline.py verify --output baseline/rdp2-final
python3 tools/check_no_private_keys.py
bash audit/run_repository_audit.sh
git diff --check
```

Ergebnisse:

- Der deterministische Vergleich aus zwei frischen `git archive HEAD`-
  Checkouts meldet `Deterministic build comparison passed`.
- Die synthetische Markerprüfung meldet Erfolg; die lokale Baselineprüfung
  verifiziert 17 Artefakte und bestätigt, dass kein privater Schlüssel kopiert
  wurde.
- Der Scan meldet `No tracked private key patterns found.`
- Der Repository-Audit wurde ohne Fehler geschrieben.
- `git diff --check` ist sauber.
- Es wurden keine Release- oder Push-Befehle ausgeführt. Das vorhandene
  `rdp2`-Dokumentationsmaterial und die Prüfwerkzeuge führen keine
  Option-Byte-Schreiboperation aus.

## Hardwaretests

Alle Hardwarelogs liegen unter
`hil-results/recovery-watchdog/2026-07-31/`. Die verwendete Plattform war der
identifizierte STM32F429 mit USART1 über `/dev/ttyUSB0`. Der Ausgangszustand
wurde mit einem read-only Flash-Backup und read-only Option-Byte-Lesung
dokumentiert. Zum Schluss wurde Stage-0 neu verifiziert geschrieben und Slot B
als normale Version 21 hergestellt.

### Tatsächlich ausgeführt

| Test | Ergebnis und Evidenz |
|---|---|
| Normalstart | Bestätigter Slot startet; `60-final-normal-boot-uart.log` zeigt `BOOT_RESET cause=SOFTWARE`, `Slot decision = CONFIRMED`, `WATCHDOG init=OK`, `HEALTH_GATE result=OK` und `SLOT_CONFIRMATION result=OK`. |
| A → B | Signiertes Paket Version 4 erfolgreich per `stm32ctl update` übertragen; `25-stm32ctl-a-to-b-v4.log`; anschließender Slot-B-Boot in `26-slot-b-v4-boot-uart.log`. |
| B → A | Signiertes Paket Version 5 erfolgreich übertragen; `27-stm32ctl-b-to-a.log` und `28-slot-a-v5-boot-uart.log`. |
| Ungültiges Paket | Manipulierte Signatur wurde vom Ziel mit `BEGIN_UPDATE rejected by target: VERIFY` abgelehnt; `29-invalid-package-target-rejection.log`; danach blieb der bestätigte Slot A bootbar, siehe `30-after-invalid-fallback-uart.log`. |
| IWDG-Trial | Testfirmware `trial_watchdog_hang` wurde signiert übertragen. `49-watchdog-trial-fallback-correct.log` zeigt `BOOT_RESET cause=IWDG`, `Slot decision = TRIAL` und `HEALTH_GATE result=NOT HEALTHY`; `56-current-reset-long.log` zeigt anschließend `BOOT_RESET cause=IWDG` und `Slot decision = FALLBACK`. |
| Recovery-Einstieg | Die Textkonsole akzeptierte `recovery` und meldete `RECOVERY READY signed-uart-update=required slot=A-bootstrap`; `61-explicit-recovery-entry-uart.log`. |
| Recovery-Protokoll | `stm32ctl info` funktionierte im aktiven Recoverydienst; `62-stm32ctl-recovery-info.log` enthält die Protokollparameter und Session State 0. |

Der erste Watchdog-Trial-Versuch (`32-watchdog-trial-campaign-uart.log`) war
als Testiteration nicht erfolgreich, weil die damalige Testfirmware zu früh
bestätigte. Diese Iteration wird ausdrücklich nicht als Fallback-Evidenz
gewertet; die Testfirmware und der Versuch wurden danach korrigiert.

### Nur vorbereitet oder noch offen

Nicht vollständig auf echter Hardware durchgeführt wurden:

- alle acht Testfirmware-Szenarien als getrennte vollständige Kampagne;
- HardFault-, Health-Fail-, No-Confirm- und Delayed-Confirm-Kampagnen mit
  vollständiger Versuchszählerdokumentation;
- jede Zeile der Metadatenkorruptionsmatrix einschließlich beider ungültiger
  Kopien;
- kontrollierte Wiederherstellung nach beiden ungültigen Metadatenkopien;
- ein Ende-zu-Ende-Lauf, bei dem Reset, Recovery und Update ausschließlich
  ohne ST-Link/OpenOCD/SWD ausgelöst wurden;
- LSI-Toleranzmessung auf dem konkreten Chip;
- physische Power-Loss-, Glitching- oder Fault-Injection-Tests.

Der Grund für den debuggerfreien Block ist die fehlende NRST-Verbindung des
Boards. Die vorhandene Resetaktion verwendete daher AIRCR über ST-Link. Der
UART-Dienst selbst wurde mit dem separaten FT232 getestet, aber dieser Ablauf
ist nicht als vollständiger debuggerfreier Nachweis zu werten.

## Debuggerfreier Test

Der signierte UART-Datenpfad, `stm32ctl info` im Recoverydienst und die
Recoveryausgabe wurden erfolgreich getestet. Ein vollständiger zweiter
Bedienlauf nur mit Versorgung, NRST, separatem USB-UART und `stm32ctl` wurde
jedoch nicht abgeschlossen, weil NRST physisch nicht angeschlossen ist und
kein sicherer externer Resetweg zur Verfügung stand. ST-Link/OpenOCD wurden in
den betroffenen Läufen für Reset und Diagnose verwendet. Damit bleibt der
Debuggerfreiheitsnachweis offen und wird nicht als RDP2-Eignung gewertet.

## Nicht getestete Punkte

Nicht getestet oder nicht abschließend bewiesen sind insbesondere:

- absichtlicher Stromausfall während jeder Flash- und Metadatenphase;
- dauerhafte RDP2-, Write-Protection- oder Option-Byte-Konfiguration;
- Verlust oder Rotation eines produktiven privaten Signierschlüssels;
- Reparatur eines beschädigten Stage-0-Bootloaders ohne SWD;
- gleichzeitige Beschädigung beider Slot-Images;
- gleichzeitige Beschädigung beider Metadatenkopien auf der Hardware;
- realer Reset über einen angeschlossenen NRST-Pin im debuggerfreien Ablauf;
- belastbare LSI-Minimal-/Maximalmessung und Erase-Zeitreserve auf jedem
  vorgesehenen Board.

## Offene Risiken

- Ein Fehler in Stage-0, insbesondere in Clock-, Vector- oder Flash-Erase-
  Logik, kann den UART-Recoverypfad unbrauchbar machen. Stage-0 ist über den
  vorhandenen Updatepfad nicht aktualisierbar und bleibt eine praktische
  irreversible Grenze.
- Ein verlorener privater Signierschlüssel verhindert neue vertrauenswürdige
  Slot-Updates; es gibt keinen dokumentierten signierten Schlüsselwechsel in
  diesem Auftrag.
- Eine außerhalb des dokumentierten LSI-Fensters liegende Clockabweichung oder
  ein zu langer Flash-Erase kann den IWDG während eines legitimen Updates
  auslösen. Die Erase-Zeitannahme muss auf Zielboards gemessen werden.
- Beschädigte Metadaten werden fail-closed behandelt, können aber bis zum
  signierten Slot-A-Recovery-Bootstrap führen; die physische Matrixabdeckung
  fehlt noch.
- Gleichzeitige Beschädigung beider Slots oder beider Metadatenkopien führt
  ohne verfügbares Stage-0 zu einer nicht mehr selbstheilbaren Situation.
- RDP2 entfernt SWD/JTAG, den ROM-Bootloader und die normale externe
  Reparaturmöglichkeit dauerhaft. Dieser Auftrag hat RDP2 nicht aktiviert.
- Option-Byte-Schutz und eine physische Power-Loss-Kampagne bleiben offen.

## Bewertung

| Bereich | Bewertung (0–10) | Begründung |
|---|---:|---|
| Boot-Recovery | 8 | Implementiert, hostgetestet und teilweise hardwaregetestet; vollständige Hardwarematrix offen. |
| Slot-Fallback | 8 | A/B-Fallback und IWDG-Fallback beobachtet; weitere Fehlstartarten offen. |
| Watchdog | 7 | Software-IWDG und realer IWDG-Reset nachgewiesen; LSI-/debuggerfreie Validierung offen. |
| Trial Boot | 8 | Persistente Grenze und Policy hostgetestet; vollständige Hardwarekampagne offen. |
| Health-Gate | 8 | Voraussetzungen implementiert und Normalfall hardwarebelegt; Fehlerfälle offen. |
| Metadatenrobustheit | 8 | Redundanz, CRC, Generation und fail-closed Policy vorhanden; Hardwarekorruption offen. |
| UART-Recovery | 8 | Signierter Recoverydienst und `stm32ctl info` hardwarebelegt; End-to-End ohne Debugger offen. |
| Hardwareevidenz | 5 | Mehrere reale Abläufe, aber kein vollständiger debuggerfreier und kein Power-Loss-Nachweis. |
| Dokumentation | 8 | Architektur, Risiken, Verfahren und Logs dokumentiert; offene Grenzen ausdrücklich markiert. |
| Open-Source-Nachvollziehbarkeit | 8 | Reproduzierbare Builds, Hosttests und klare Statuskennzeichnung vorhanden. |

## Sicherheitsgrenze

RDP2 bleibt deaktiviert. Keine irreversible Option-Byte-Änderung wurde
vorgenommen. Keine physische Power-Loss-Kampagne wurde durchgeführt.

RECOVERY SOFTWARE COMPLETE – HARDWARE VALIDATION REMAINS
