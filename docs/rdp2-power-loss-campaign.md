# Power-Loss-Testkampagne

Die Kampagne wird auf einem entbehrlichen, eindeutig beschrifteten
STM32F429-Testchip mit kontrollierter Versorgung und einem externen USB-UART-
Adapter durchgeführt. SWD, JTAG, ST-Link, OpenOCD, GDB und ROM-Bootloader sind
während der Kampagne nicht zulässig. Die Stromunterbrechung erfolgt manuell
oder durch ein externes, strombegrenztes Laborgerät; es wird keine Software-
Routine zum Abschalten oder Glitchen des Zielchips erzeugt.

Ausgangslage: Slot A ist `CONFIRMED`, Slot B ist der inaktive Kandidat für ein
Update, die Baseline ist gesichert. Jede Unterbrechung erhält eine eindeutige
Versuchsnummer. Vor und nach jedem Versuch werden Versorgung, NRST, UART-
Richtung, Paketversion und die beobachtete UART-Sequenz protokolliert.

## Erwartungsmatrix

| Phase der Unterbrechung | Erwarteter Zustand nach Neustart | Erwarteter aktiver Slot | Erwartete Metadaten | UART-Erwartung |
|---|---|---|---|---|
| Paketbeginn, vor `BEGIN_UPDATE` | keine Updatewirkung | A | `CONFIRMED` | normaler Stage-0-/EXP066-Start |
| Metadaten `WRITING` | Kandidat unbootbar, A bleibt gültig | A | `WRITING`, candidate B oder alte gültige Kopie | ggf. unvollständige ACK/NACK-Ausgabe; danach Fallback-Start |
| Flash-Erase | B gelöscht oder teilweise gelöscht, A unverändert | A | `WRITING` oder ältere `CONFIRMED`-Kopie | Update-Text darf fehlen; nächster UART-Start muss A zeigen |
| frühes Flash-Program | B unvollständig | A | `WRITING` | kein Start von B erlaubt; A-Fallback |
| mittleres Flash-Program | B unvollständig | A | `WRITING` | wie oben |
| letztes Flash-Program | B möglicherweise vollständig, aber noch nicht freigegeben | A | `WRITING` | B darf noch nicht als Trial starten |
| Paketabschluss, vor `FINISH_UPDATE` | B-Daten ggf. vollständig, nicht freigegeben | A | `WRITING` | nächster Start A |
| Verifikation | Hash/Signatur noch nicht committed | A | `WRITING` | Prüf-/ACK-Ausgabe darf abgeschnitten sein; A startet |
| unmittelbar vor `CANDIDATE_READY` | kein freigegebener Kandidat | A | `WRITING` | A startet |
| unmittelbar nach `CANDIDATE_READY` | B ist Kandidat, Trial noch nicht begonnen | A | `CANDIDATE_READY`, candidate B | nächster Stage-0-Start darf B als `TRIAL` wählen |
| erster Trial Boot | Trial-Versuch vor oder nach Versuchszählercommit | A als Fallback | `PENDING_TRIAL`, candidate B, Versuchszähler reduziert | B darf starten; bei erneutem Reset wird B nur innerhalb des Versuchslimits probiert |
| vor Health-Gate-Erfolg | B läuft nicht bestätigt | A als Metadaten-Fallback | `PENDING_TRIAL` | B-Ausgabe kann fehlen; nach Reset Trial/Fallback nach Policy |
| vor Slot-Bestätigung | B läuft, Bestätigung noch nicht committed | A als Fallback | `PENDING_TRIAL` | `confirmation` darf fehlen; kein dauerhafter Wechsel ohne Commit |
| nach Slot-Bestätigung | neuer Slot dauerhaft bestätigt | B | `CONFIRMED`, active B, candidate `NONE` | B-Start mit `Slot policy = OK` und EXP066 |

„Aktiver Slot“ bezeichnet den bestätigten Fallback in den Metadaten. Während
`CANDIDATE_READY`/`PENDING_TRIAL` darf die laufende Trial-Firmware B sein, ohne
dass B bereits als dauerhaft aktiver Slot gilt.

## Erlaubtes Verhalten

- Die Update-UART-Session endet ohne vollständigen ACK, wenn die Versorgung
  während eines Frames oder Flash-Vorgangs ausfällt.
- Eine neuere Metadatenkopie kann `WRITING`, `CANDIDATE_READY` oder
  `PENDING_TRIAL` anzeigen; die ältere bestätigte Kopie bleibt als Fallback
  erhalten, sofern der Flash-Vorgang nicht selbst einen Hardwarefehler erzeugt.
- Stage 0 verwirft einen unvollständigen, ungültig signierten oder nicht
  bestätigten Kandidaten und startet die letzte bestätigte Firmware.
- Nach stabiler Versorgung kann der Host den fehlgeschlagenen Versuch mit einem
  neuen, strikt höheren Paket wiederholen, sofern die bestätigte Firmware noch
  läuft.

## Nicht erlaubtes Verhalten

- Start eines unvollständigen oder ungültig signierten Kandidaten.
- Verlust des letzten gültigen bestätigten Slots durch einen Updateversuch.
- `CANDIDATE_READY` vor vollständigem Flash-Readback, Hash- und Signaturcheck.
- Überschreiben des aktiven Slots oder des Stage-0-Bereichs.
- Erwartung eines Debugger-, ST-Link- oder ROM-Bootloader-Recoverywegs.
- UART- oder Clockzustand, der nach normalem Reset dauerhaft keinen erneuten
  `stm32ctl`-Handshake ermöglicht.

## Protokollvorlage

Für jede Phase werden mindestens diese Felder erfasst: Versuch-ID, Phase,
Paket-/Imageversion, Versorgung aus/an Zeitpunkte, NRST-Zustand, letzte
vollständige `SUPD`-Antwort, beobachtete Stage-0-Zeilen, beobachtete EXP066-
Zeilen, erwarteter/gefunden­er Slot, erwarteter/gefunden­er Metadatenzustand,
Pass/Fail, Recovery ohne Debugger und Verweis auf den UART-Log. Binär- oder
Flashdumps bleiben lokale Labordaten und werden nicht in Git abgelegt.

## Recovery ohne Debugger

Nach jeder Unterbrechung Versorgung stabilisieren und nur über UART neu starten.
Wenn A weiterhin startet, Status dokumentieren und ein vollständiges,
signiertes B-Paket erneut übertragen. Wenn B nach Bestätigung startet, gilt B
als neuer Ausgangszustand; für einen Rückweg wird ein höher versioniertes,
signiertes A-Paket benötigt. Wenn kein gültiger Slot startet oder beide
Metadatenkopien unlesbar sind, ist der Versuch ein Blocker: Die Plattform hat
für diesen Zustand keinen dokumentierten debuggerfreien Reparaturweg.
