# USART1 und RDP2

RDP2 sperrt die externen Debug-/Programmierzugänge des Zielchips; es sperrt
nicht grundsätzlich die normale USART1-Peripherie, die von der laufenden
Firmware initialisiert wird.

- Normales USART1 der Firmware bleibt grundsätzlich nutzbar.
- Die UART-Ausgaben der Anwendung und die bestehende EXP066-Telemetrie bleiben
  möglich.
- Der eigene UART-Updater bleibt möglich, solange Stage 0 korrekt startet,
  Clock und USART1 korrekt initialisiert werden und mindestens ein bestätigter
  Slot vorhanden ist.
- Der STM32-ROM-Systembootloader über UART ist unter RDP2 kein Rettungsweg und
  darf nicht vorausgesetzt werden.
- ST-Link-Virtual-COM ist ein separates USB-/Debug-Zubehör und nicht mit einer
  direkt angeschlossenen USART1-Verbindung gleichzusetzen.
- SWV/SWO ist unter RDP2 kein verfügbarer Diagnosekanal.
- Kein Build-, Update-, Test- oder Recovery-Verfahren darf Debuggerzugriff
  voraussetzen.

## Robuste Betriebsparameter

Die aktuelle Referenz verwendet USART1 auf PA9/PA10 mit 115200 Baud, 8N1.
Stage 0 initialisiert zuerst den Board-Clockpfad und danach USART1; der eigene
Updatepfad hat ein begrenztes Entry-Fenster, bounded Frames und byteweise
Timeouts. `stm32ctl` verwendet für die hardwaregemessene
Kandidaten-Sektoroperation standardmäßig mindestens 15 Sekunden Antworttimeout
und wiederholt nur idempotente Leseanfragen. Zustandsändernde Frames werden bei
Timeout nicht blind wiederholt; der Operator muss Status und Reset beobachten.

Vor RDP2 müssen auf dem finalen Board ohne ST-Link-VCP nachgewiesen werden:

- stabile 115200-Baud-Ausgabe nach Power-on und NRST;
- `HELLO`, `GET_STATUS`, Update und Reset mit separatem USB-UART;
- Verhalten bei absichtlich verzögerten bzw. fragmentierten Frames;
- Clock-/Baudratestabilität während Flash-Erase und -Program;
- NRST während `WRITING`, Trial Boot und Bestätigung;
- Wiederaufnahme über UART nach jeder kontrollierten Stromunterbrechung.

Diese Punkte sind Hardwarevalidierung, nicht durch Hosttests oder die bloße
Anwesenheit eines UART-Geräts bewiesen.
