# EXP042 – Bootloader Architecture Audit

## Ergebnis

- EXP013: 16-KiB-Bootloader ab 0x08000000.
- EXP014: 16-KiB-Bootloader mit CRC-geschütztem Image.
- EXP019: 32-KiB-Bootloader mit signiertem Image.
- EXP022: 32-KiB-Bootloader mit Signatur- und Rollback-Prüfung.

## Speicherlayout

- Bootloaderbasis: 0x08000000
- EXP013/014 Bootloaderende: 0x08003FFF
- Reservierter Bereich bei EXP013/014: 0x08004000–0x08007FFF
- EXP019/022 Bootloaderende: 0x08007FFF
- Imagebasis und Manifest: 0x08008000
- Manifestgröße: 96 Byte
- Signaturadresse: 0x08008060
- Payload und Vektortabelle: 0x08008200
- Maximales Image-Ende: 0x08100000
- SRAM: 0x20000000–0x2001FFFF

## Verifikationskette

- Prüfung von Magic und Header-Version.
- Prüfung der Image-Version.
- Prüfung von Vektoradresse und Imagegröße.
- Prüfung von Stackpointer und Reset-Vektor.
- SHA-512-Prüfung des Payloads.
- Ed25519-Signaturprüfung.

## Rollback-Schutz

- EXP022 definiert MIN_IMAGE_VERSION = 2.
- Images unter Version 2 werden abgelehnt.
- Der Mindestwert ist derzeit fest in der Bootloader-Firmware kompiliert.
- Es existiert noch kein dauerhaft aktualisierter monotoner Versionszähler.

## Sicherheitsbewertung

- Das signierte Bootkonzept bietet eine gute Grundlage für Secure Boot.
- Die Recovery-Schnittstelle muss authentifiziert und ausdrücklich aktiviert werden.
- Ein universeller unauthentifizierter Speicherlesezugang darf nicht vorgesehen werden.
- Für ein wiederverwendbares Laborboard darf RDP Level 2 nicht aktiviert werden.

## Status

EXP042 wurde vollständig offline durchgeführt.
