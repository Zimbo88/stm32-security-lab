# Manuelle RDP2-Provisionierungscheckliste

Diese Datei ist ausschließlich eine manuell abzuarbeitende Checkliste. Sie
aktiviert RDP2 nicht, enthält keinen Programmer-Befehl und darf nicht als
automatisches Verfahren in CI, Make oder Scripts eingebunden werden.

## Freigaben und Material

- [ ] Entbehrlicher STM32F429-Testchip vorhanden.
- [ ] Mindestens ein ungeschütztes Referenzboard vorhanden.
- [ ] Vollständiges, verifiziertes Backup vorhanden.
- [ ] Git-Bundle des geprüften Branches vorhanden.
- [ ] Privater Signierschlüssel mehrfach sicher gesichert.
- [ ] Public-Key-Fingerprint dokumentiert und gegen Stage 0 geprüft.
- [ ] Referenzartefakte mit `tools/rdp2_baseline.py` erzeugt und verifiziert.
- [ ] Genaue Chipkennung, Revision, Flashgröße und Boardrevision dokumentiert.
- [ ] Board eindeutig beschriftet.

## Software- und UART-Nachweise

- [ ] Referenzupdate A → B erfolgreich.
- [ ] Referenzupdate B → A erfolgreich.
- [ ] Ungültige Signatur erfolgreich abgelehnt.
- [ ] Rollback erfolgreich abgelehnt.
- [ ] Falscher Zielslot erfolgreich abgelehnt.
- [ ] Unvollständiges Paket erfolgreich abgelehnt.
- [ ] Abgebrochenes Update lässt den bestätigten Slot starten.
- [ ] Trial Boot, Health Gate und Slot-Bestätigung protokolliert.
- [ ] UART mit separatem USB-UART getestet; ST-Link-VCP nicht verwendet.
- [ ] Recovery ohne SWD/JTAG/ST-Link/ROM-Bootloader bestanden.
- [ ] Baudrate, Clock-Initialisierung, Timeout und NRST-Verhalten protokolliert.

## Power-Loss- und Artefaktnachweise

- [ ] Power-Loss-Kampagne für alle in `docs/rdp2-power-loss-campaign.md`
      aufgeführten Phasen bestanden.
- [ ] Bootloaderhash dokumentiert.
- [ ] Slot-A-Hash dokumentiert.
- [ ] Slot-B-Hash dokumentiert.
- [ ] Updatepaket- und Public-Key-Hashes dokumentiert.
- [ ] Erwartete UART-Startausgabe dokumentiert.
- [ ] Erwartete Slot-Metadaten dokumentiert.
- [ ] Option Bytes vor der Aktivierung ausschließlich lesend dokumentiert.
- [ ] Private Schlüssel, Dumps und lokale Labordaten aus Git ausgeschlossen.
- [ ] Keine Datei oder CI-Stufe schreibt Option Bytes oder setzt RDP.

## Irreversible Konsequenzen

- [ ] Verlust von SWD/JTAG und normalem ST-Link-Flashzugriff akzeptiert.
- [ ] Verlust des STM32-ROM-Bootloader-Recoverywegs akzeptiert.
- [ ] Option-Byte-Recovery und normales Reflashing sind nicht verfügbar.
- [ ] Ein defekter Stage-0-Bootloader ist praktisch irreparabel.
- [ ] Verlust des privaten Signierschlüssels beendet die zukünftige
      Updatefähigkeit für den eingebetteten Public Key.
- [ ] Fehler in Clock/UART, Metadaten oder Flash-Erase können den einzigen
      verbleibenden Bedienweg abschneiden.
- [ ] Die Rückkehr von RDP2 wird dauerhaft akzeptiert.
- [ ] Die Aktivierung erfolgt erst nach einer separaten manuellen Prüfung und
      niemals durch dieses Repository automatisch.

> RDP Level 2 is irreversible on the target STM32F429.
> Do not continue unless loss of SWD/JTAG, ROM bootloader access,
> option-byte recovery and normal reflashing is permanently accepted.
