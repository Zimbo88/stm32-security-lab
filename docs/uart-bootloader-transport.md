# Bootloader UART Transport

Der Bootloader verwendet USART1 im Polling-Betrieb.

- TX: PA9, Alternate Function AF7.
- RX: PA10, Alternate Function AF7, interner Pull-up aktiv.
- Baudrate: 115200 Baud.
- Clock-Basis: `uart_init()` nutzt `board_clock_get_sysclk_hz()`. Im aktuellen
  Board-Clock-Stand bleibt der Controller auf HSI mit 16 MHz, daraus ergibt
  sich `USART1_BRR = 0x008B`.

## Empfangsverhalten

Der Empfang ist bewusst interrupt- und DMA-frei. Timeouts sind als Poll-Budget
definiert, nicht als Wandzeit:

- `uart_getc_nonblocking()` prueft einmal auf ein Byte.
- `uart_getc_timeout()` versucht hoechstens `timeout_polls` Polls.
- `uart_read_timeout()` verwendet das Poll-Budget pro erwartetes Byte und gibt
  die Anzahl bereits gelesener Bytes zurueck.
- `uart_flush_rx()` verwirft ausstehende RX-Daten und bricht intern nach einer
  festen Drain-Grenze ab.

USART-Fehler werden vor der Datenuebergabe erkannt. Overrun, Framing, Noise und
Parity werden durch die uebliche STM32F4-Sequenz Statusregister lesen,
Datenregister lesen behandelt; das betroffene Byte wird verworfen und der
Fehlerstatus an den Aufrufer gemeldet.

## Transportneutrale Reader-Schicht

`byte_reader_t` kapselt einen nichtblockierenden Byte-Reader mit Kontextzeiger.
Parser koennen spaeter gegen `byte_reader_getc_timeout()` und
`byte_reader_read_timeout()` implementiert werden. Auf dem Ziel wird
`uart_byte_reader_init()` verwendet; Host-Tests koennen stattdessen einen Fake-
Reader ohne USART-Register einsetzen.

## Bootloader-Einstieg

Nach `board_clock_init()` und `uart_init()` oeffnet der Bootloader ein kurzes,
begrenztes UART-Einstiegfenster. Ein gueltiges `HELLO`-Frame des
UART-Binaerprotokolls mit Sequenznummer 0 aktiviert den Update-Modus. Eine
vollstaendige Textzeile aktiviert die read-only Diagnosekonsole. Ohne
gueltiges `HELLO`, ohne vollstaendige Textzeile, bei UART-Rauschen oder nach
Ablauf des Poll-Budgets laeuft die normale Secure-Boot-Sequenz weiter.

Das Einstiegfenster ist aktuell rein zeit- beziehungsweise poll-basiert:

- keine DMA- oder Interrupt-Abhaengigkeit,
- kein ST-ROM-Bootloader,
- keine Option-Byte- oder RDP-Aenderung,
- kein physischer Update-GPIO.

Ein Update-GPIO wird bewusst noch nicht gewaehlt, weil das konkrete Board-
Pinout im Repository noch nicht eindeutig genug dokumentiert ist. Bis dahin
werden Update- und Diagnosemodus nur durch das begrenzte UART-Einstiegfenster
aktiviert.

Im Update-Modus gibt der Bootloader keine menschlichen Diagnosezeilen auf
derselben UART-Verbindung aus. Antworten sind ausschliesslich binaere
ACK/NACK-Frames. Nach erfolgreichem `FINISH_UPDATE` muss der Installer
`CANDIDATE_READY` committed haben; danach fordert der Bootloader ueber
AIRCR/SYSRESETREQ einen kontrollierten Systemreset an.

Die Textkonsole ist in `docs/uart-diagnostic-console.md` beschrieben. Sie hat
keine Flash-Schreib-, Erase-, Slot-, Versions-, Option-Byte- oder RDP-Befehle.
