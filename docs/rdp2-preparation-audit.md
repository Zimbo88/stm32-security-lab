# RDP2-Vorbereitungs-Audit

Stand: 2026-07-31, Branch `research/rdp2-final-preparation`.

Dieses Audit beschreibt den Stand vor einer möglichen manuellen RDP2-
Provisionierung. RDP2 wurde in diesem Auftrag nicht aktiviert. Die historischen
RDP1-/Option-Byte-Logs im Repository sind Laborhistorie und kein Nachweis für
RDP2-Betrieb.

## Bereits implementiert

- Stage-0-Bootloader `firmware/exp045_bootloader_v2` mit SHA-512-, Ed25519-,
  Manifest-, Vektor-, MSP-, Slot- und Versionsprüfung.
- Redundante Boot-Metadaten mit `CONFIRMED`, `WRITING`, `CANDIDATE_READY`,
  `PENDING_TRIAL` und `REJECTED_INVALID`.
- Dual-Slot-Layout für STM32F429IGT6/1 MiB: Slot A ab `0x08020000`, Slot B ab
  `0x08080000`; der Installer leitet den inaktiven Slot aus bestätigten
  Metadaten ab und akzeptiert keinen vom Host vorgegebenen Flash-Zielbereich.
- UART1-Binary-Protokoll `SUPD` mit CRC32, Sequenznummern, bounded Frames,
  ACK/NACK, Timeouts und `stm32ctl`.
- Streaming-Installer: Prüfung vor Erase, `WRITING` vor Kandidaten-Erase,
  blockweises Flash-Programmieren, Readback, SHA-512-/Signaturprüfung und
  `CANDIDATE_READY` erst am Ende.
- Trial-Boot, Versuchszähler, Health Gate und Slot-Bestätigung in EXP066.
- RSM, Telemetrie und schreibgeschützte UART-Diagnose.
- Synthetische Forschungsmarker mit ELF-Symbolen und Baseline-/Dump-Tooling.

## Nachweisstatus

| Bereich | Hostgetestet | Hardwarevalidiert | Vor RDP2 noch offen |
|---|---:|---:|---:|
| Manifest, Hash, Ed25519, Vektorprüfung | Ja | Ja, RDP0 | Wiederholungsnachweis am finalen Testchip |
| A → B und B → A über eigenen UART-Updater | Ja | Ja, RDP0 mit ST-Link im Umfeld | UART-only-Wiederholung ohne Debugger |
| Nur inaktiven Slot überschreiben | Ja | Ja, RDP0 | Nachweis mit separatem USB-UART als alleiniger Bedienweg |
| Abbruch/Reset während `WRITING` | Ja | Ja, UART-/ST-Link-Test | physischer Power-Loss in allen Phasen |
| Trial Boot, Health Gate, Bestätigung | Ja | Ja, RDP0 | Power-Loss vor Bestätigung auf dem entbehrlichen Chip |
| Metadatenredundanz und ungültige Records | Ja | teilweise über Readback | vollständige Stromunterbrechung bei Commit-Grenzen |
| Clock, USART1, Reset/NRST ohne Debugger | Ja, Code-/Hosttests | UART-Startnachweise vorhanden | vollständige elektrische Wiederholung ohne ST-Link-VCP/SWD |
| RDP2-Verhalten | Nein | Nein | vollständig offen; RDP2 bleibt deaktiviert |

## Dauerhafte Sperrrisiken

- Ein fehlerhafter oder beschädigter Stage-0-Bootloader ist über den normalen
  Slot-Updatepfad nicht aktualisierbar. Nach RDP2 gibt es dafür im Projekt keinen
  vorgesehenen Reparaturweg.
- Der private Ed25519-Schlüssel ist für jedes zukünftige Slot-Update zwingend.
  Sein Verlust beendet die praktische Updatefähigkeit, sofern kein separater
  Schlüsselwechselmechanismus eingeführt wird.
- Fehler in Clock- oder USART1-Initialisierung, UART-Pinout, Baudrate oder
  Reset-Timing können nach RDP2 den einzigen vorgesehenen Bedienweg abschneiden.
- Beschädigte beide Metadatenkopien oder ein Flash-Fehler, der den bestätigten
  Slot zerstört, können zu „kein bootbarer Slot“ führen.
- RDP2 entfernt die üblichen SWD-/JTAG-/ST-Link-/ROM-Bootloader-Wege dauerhaft;
  eine Rückkehr zu RDP0 ist auf dem Ziel nicht vorgesehen.

## Vorhandene Hardwareevidenz richtig einordnen

Die vorhandenen Release- und HIL-Dokumente belegen einen RDP0-Lauf auf einem
STM32F429IGT6-Klasse-Board. Dabei wurden die positiven Slotwechsel und viele
negative UART-Fälle mit ST-Link im Testumfeld validiert. Physische
Power-Loss-Phasen und ein vollständig debuggerfreier Ablauf wurden dort nicht
als bestanden nachgewiesen. Ein aktueller nichtmutierender UART-Handshake auf
`/dev/ttyUSBx` antwortete am 2026-07-31 mit Timeout; deshalb wurde kein
mutierender Hardwaretest ausgeführt.

## Referenzartefakte

`tools/rdp2_baseline.py` erzeugt nur lesend bzw. durch lokale Builds einen
ignorierten Ordner `baseline/rdp2-final/`. Er enthält öffentliche Artefakte,
ELF-Symboltabellen, Markerberichte, Speicherlayout, Manifeste und SHA-256-
Hashes. Private Schlüssel, Dumps, Option-Byte-Schreibvorgänge und
Programmierbefehle sind ausdrücklich ausgeschlossen.
