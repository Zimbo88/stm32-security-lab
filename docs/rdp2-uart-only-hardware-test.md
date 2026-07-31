# Hardwaretest ohne Debugger

Dieses Verfahren simuliert den späteren Betrieb eines bereits provisionierten
RDP2-Testchips. Es verwendet ausschließlich:

- Versorgung und GND;
- NRST als manuellen Reset;
- USART1 TX und USART1 RX über einen separaten USB-UART-Adapter;
- `stm32ctl` und ein gewöhnliches UART-Terminal mit 115200 8N1.

Nicht anschließen oder verwenden: SWD, JTAG, ST-Link-Flashzugriff, ST-Link-
Virtual-COM, OpenOCD, GDB, STM32CubeProgrammer zum Flashen und den
STM32-ROM-Systembootloader. Ein ST-Link darf höchstens vollständig getrennt
als nicht benötigtes Entwicklungszubehör vorhanden sein.

## Voraussetzungen und Ausgangszustand

Der Ausgangszustand muss vorab auf einem ungeschützten Referenzboard oder
durch eine separat freigegebene Pre-RDP2-Provisionierung hergestellt werden:

- Stage 0 ist unverändert und gehasht dokumentiert.
- Slot A ist eine gültige, signierte und bestätigte Firmware.
- Slot B ist entweder leer/ungültig oder enthält eine ältere gültige Version.
- Beide Metadatenkopien sind lesbar; erwartet wird `CONFIRMED`, aktiver Slot A,
  Kandidat `NONE`, Versuchszähler 0, Bestätigung 1.
- Das Baseline-Verzeichnis und der externe Signierschlüssel sind vorhanden.

Nach diesem Ausgangszustand darf die Testsequenz ohne Debugger nur noch den
eigenen UART-Updater verwenden. Ein leerer Chip ist kein UART-only-
Ausgangszustand, weil Stage 0 und die erste Slot-A-Metadatenprovisionierung
außerhalb des normalen Updatepfads liegen.

## Ablauf

1. Board eindeutig beschriften, Versorgung aus, nur Versorgung/GND/NRST/USART1
   anschließen. TX und RX kreuzen; Pegel und gemeinsame Masse prüfen.
2. Versorgung einschalten. Im UART-Terminal muss zuerst der Stage-0-Banner,
   danach `Slot policy = OK`, die akzeptierte Signatur und die EXP066-
   Startausgabe erscheinen. `confirmation OK` bzw. `confirmation status`
   weist auf einen bestätigten Slot hin.
3. A → B: mit einer für Slot B signierten Version, die strikt höher als die
   bestätigte A-Version ist, übertragen:

   ```sh
   PYTHONPATH=tools python3 -m stm32ctl --port "$UART" --baud 115200 \
     --timeout 15 update --package "$SLOT_B_UPDATE"
   ```

   Erwartet wird ein erfolgreiches `FINISH_UPDATE` mit abschließendem Status.
   Danach `stm32ctl reset` oder NRST auslösen. Stage 0 muss `TRIAL` für Slot B
   starten; nach Health Gate und Bestätigung muss B `CONFIRMED` sein.

4. B → A: eine strikt höhere Slot-A-Version übertragen, anschließend erneut
   resetten und `TRIAL` → Health Gate → `CONFIRMED` für Slot A nachweisen.
5. Falsches Ziel: ein Slot-A-Paket während aktivem Slot A anbieten. Der Host
   muss es lokal ablehnen oder das Ziel muss mit `VERIFY`/`STATE` ablehnen. Der
   aktive Slot darf sich nicht ändern.
6. Ungültiges Paket / ungültige Signatur: eine Kopie eines gültigen Pakets lokal bitweise ändern
   und als Paketdatei angeben. `stm32ctl` muss mit Fehler enden; kein Kandidat
   darf `CANDIDATE_READY` erreichen.
7. Rollback: ein gültiges, aber gleiches oder niedrigeres Versionspaket
   anbieten. Erwartet wird `ROLLBACK`; der bestätigte Slot und die Metadaten
   bleiben unverändert.
8. Unvollständiges Paket: eine abgeschnittene Paketkopie übertragen. Erwartet
   wird lokale Paketablehnung oder `LENGTH`/`VERIFY`; kein Flash-Ziel darf als
   bootbarer Kandidat markiert werden.
9. Update-Abbruch: nach Beginn und mindestens einem `WRITE_BLOCK` die
   Übertragung mit Ctrl-C beenden oder den USB-UART kurz trennen. Danach
   Versorgung aus/ein oder NRST. Der zuvor bestätigte Slot muss starten; ein
   unvollständiger Kandidat darf nicht gestartet werden.
10. Erneuter Neustart: ohne Debugger wiederholt NRST betätigen und je Start
    Stage-0- und EXP066-UART-Ausgabe protokollieren. Mindestens ein gültiger
    bestätigter Slot muss jedes Mal starten.

Die Binary-Update-Session gibt keine menschlich lesbaren Diagnosetexte auf
demselben UART aus. Während des Updates sind `SUPD`-ACK/NACK-Frames und danach
die normale Startausgabe zu erwarten. Die Slot-/Metadatenbestätigung erfolgt
über die bestehende EXP066-Diagnosekonsole, nicht über einen neuen Updatekanal.

## Passkriterien

- A → B und B → A sind ausschließlich mit `stm32ctl` erfolgreich.
- Der Host kann weder Stage 0 noch den aktiven Slot als Ziel auswählen.
- Falsche Signatur, falscher Slot, Rollback und unvollständige Daten werden
  fail-closed abgelehnt.
- Nach Abbruch und Reset startet weiterhin die letzte bestätigte Firmware.
- Kein Testschritt benötigt SWD/JTAG/ST-Link/ROM-Bootloader.
- Jede Ausgabe und jede Paket-/Versions-/Hashreferenz ist im Testprotokoll
  erfasst.

## Nicht durch diesen Ablauf bewiesen

Der Ablauf beweist nicht die elektrische Power-Loss-Sicherheit, die
Unveränderlichkeit des Stage-0-Bootloaders oder die erfolgreiche Aktivierung
von RDP2. Diese Punkte benötigen die getrennte Checkliste und Kampagne.
