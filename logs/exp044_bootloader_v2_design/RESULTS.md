# EXP044 – Bootloader v2 Architecture Design

## Ziel

Entwurf eines wartbaren Secure-Boot-Systems für das Laborboard.
Das Board soll signierte Firmware starten, kontrolliert aktualisiert werden
und nach Experimenten weiterhin wiederverwendbar bleiben.

## Vertrauensanker

- Der Bootloader enthält ausschließlich öffentliche Verifikationsschlüssel.
- Private Signaturschlüssel verbleiben außerhalb des Mikrocontrollers.
- Nur korrekt signierte Firmware darf gestartet oder installiert werden.

## Betriebsmodi

### Normal Boot

- Manifest und Payload werden vollständig geprüft.
- Nur bei erfolgreicher Prüfung erfolgt der Sprung zur Anwendung.
- Fehler führen zu einem fail-closed Zustand.

### Authenticated Update

- Updates werden zunächst vollständig empfangen.
- Länge, Zielbereich, Versionsnummer, Hash und Signatur werden geprüft.
- Erst danach darf Flash gelöscht oder programmiert werden.
- Der Bootloaderbereich bleibt vom Update ausgeschlossen.

### Physical Recovery

- Recovery wird nur durch eine dokumentierte physische Aktion aktiviert.
- Beispiele: Recovery-Taster beim Reset oder definierter Jumper.
- Auch im Recovery-Modus werden ausschließlich signierte Images akzeptiert.
- Es existiert kein universeller Speicherlesebefehl.

## Empfohlenes Flashlayout

- 0x08000000–0x08007FFF: Bootloader, 32 KiB
- 0x08008000–0x080081FF: Manifest und Signaturbereich
- 0x08008200–0x080FFFFF: Anwendungs-Payload
- Bootloader und Anwendung dürfen sich nicht überschneiden.

## Verifikationsreihenfolge

1. Manifest-Struktur und Formatversion
2. Magic und unterstützter Algorithmus
3. Zieladresse und Größenbegrenzung
4. Image-Version und Rollback-Regel
5. Initialer MSP und Reset-Vektor
6. Payload-Hash
7. Digitale Signatur
8. Freigabe für Boot oder Installation

## Rollback-Konzept

- Die bisherige compile-time Mindestversion wird zunächst beibehalten.
- Eine spätere persistente Versionsuntergrenze benötigt atomare Updates.
- Stromausfall darf den gespeicherten Zustand nicht unbrauchbar machen.
- Eine Version darf niemals vor erfolgreicher Installation erhöht werden.

## Update-Sicherheitsregeln

- Keine Schreiboperation ohne vollständig validierten Header.
- Keine Adressen außerhalb des Anwendungsbereichs.
- Schutz gegen Integerüberlauf bei Adresse plus Länge.
- Begrenzte Paketgröße und definierte Timeouts.
- Nach Programmierung erneute Hash- oder Signaturprüfung aus dem Flash.

## Wiederverwendbarkeit des Laborboards

- RDP Level 2 wird nicht verwendet.
- Recovery und Wartung bleiben dokumentiert möglich.
- Wiederherstellungsabbilder und Hashwerte werden versioniert archiviert.
- Schutzänderungen erfolgen nur in separaten, ausdrücklich bestätigten Experimenten.

## Nicht-Ziele

- Kein versteckter Wartungszugang.
- Kein unauthentifizierter Debug- oder Speicherzugriff.
- Kein Mechanismus zur Umgehung aktivierter Schutzfunktionen.

## Ergebnis

Die v2-Architektur trennt Boot, Update und physisch aktiviertes Recovery.
Alle ausführbaren Images bleiben signaturpflichtig.
EXP044 wurde vollständig offline durchgeführt.
