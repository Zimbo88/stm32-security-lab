# Updatefähigkeit und Schlüssel nach RDP2

RDP2 ist in diesem Repository nicht aktiviert. Dieses Dokument beschreibt nur
die Annahmen für die spätere manuelle Prüfung.

## Zwingend benötigte Schlüssel und Sicherungen

Für ein Slot-Update benötigt der Host den privaten Ed25519-Signierschlüssel.
Der Zielchip benötigt ausschließlich den bereits in Stage 0 eingebetteten
öffentlichen Schlüssel. Zusätzlich müssen sicher und mehrfach getrennt gesichert
werden:

- der private Signing Seed bzw. die gleichwertige Schlüsselablage;
- der dokumentierte SHA-256-Fingerprint des öffentlichen Schlüssels;
- unveränderte Stage-0-ELF-/BIN-/HEX-Artefakte und deren Hashes;
- Slot-A-/Slot-B-ELF-/BIN-/HEX-Artefakte, signierte Updatepakete und Manifeste;
- Speicherlayout, Toolchain-Version, Build- und Git-Identität;
- die RDP0-Referenzkarte, das ungeschützte Board und die manuelle
  RDP2-Checkliste.

Private Schlüssel werden niemals in `baseline/rdp2-final/`, Git, Logs,
Manifesten, Testausgaben oder UART übertragen. `tools/rdp2_baseline.py` prüft
und kopiert nur öffentliche Artefakte; bei `--build` wird ein vom Operator
angegebener Seed nur als Eingabe an den vorhandenen Signierweg übergeben.

## Build und Signaturweg

1. Stage 0 und die jeweilige Slot-Firmware mit dem dokumentierten
   `stm32f429_1m`-Layout bauen.
2. Slot A und Slot B getrennt linken; die jeweilige Vektoradresse bindet das
   Paket an genau diesen Slot.
3. Das vorhandene `tools/update_package.py build` verwendet den privaten Seed,
   erzeugt Manifest, Payload-Hash und Ed25519-Signatur. `stm32ctl` signiert
   nicht.
4. Das vorhandene `tools/update_package.py verify` bzw. `make
   verify-update-package` prüft offline gegen den eingebetteten öffentlichen
   Schlüssel.
5. `stm32ctl update --package ...` prüft lokal und überträgt danach über
   USART1. Der Ziel-Installer prüft erneut, ermittelt selbst den inaktiven
   Slot, löscht nur dessen Sektoren, programmiert, liest zurück und committet
   erst danach `CANDIDATE_READY`.

Die Versionsregel ist strikt monoton relativ zur bestätigten Version: gleiche
oder niedrigere Versionen werden als Rollback abgelehnt. Ein Paket für den
falschen Slot wird abgelehnt. Der Host darf keine Flashadresse und keinen
Zielslot als Updateparameter vorgeben.

## Verlust des privaten Schlüssels

Geht der private Schlüssel verloren, können keine neuen gültigen Slotpakete für
den eingebetteten öffentlichen Schlüssel erzeugt werden. Die bereits
installierte Firmware läuft weiter, sofern sie nicht beschädigt wird; ein
weiteres Update oder eine Reparatur eines defekten Slots ist ohne den passenden
privaten Schlüssel praktisch nicht möglich. Es gibt derzeit keinen
Schlüsselwechsel-, Revokations- oder Recovery-Key-Pfad.

## Was bleibt aktualisierbar?

- Slot A und Slot B bleiben über den bestehenden signierten UART-Updater
  aktualisierbar, jeweils nur wenn der andere Slot bestätigt und funktionsfähig
  als Fallback vorhanden ist.
- Die Slot-Metadaten werden als Teil des bestehenden Trial-/Bestätigungsablaufs
  aktualisiert.
- Die Anwendung kann ihre UART-Telemetrie und den Runtime Security Monitor
  weiter ausführen, wenn sie korrekt startet.

## Praktisch unveränderlich

- Stage 0 liegt außerhalb der Update-Schreibbereiche und wird vom Installer
  nicht als Kandidat akzeptiert.
- Das Speicherlayout, die eingebettete Public-Key-Vertrauenswurzel und der
  Stage-0-Code werden durch den vorhandenen Slotpfad nicht ersetzt.
- Option Bytes, RDP-Zustand und Debug-Sperren sind kein Bestandteil von
  `stm32ctl` und dürfen durch diesen Workflow nicht verändert werden.

## Stage-0-Grenze

Der Stage-0-Bootloader kann über den vorhandenen UART-Slot-Updatepfad nicht
aktualisiert werden. Nach einer späteren RDP2-Aktivierung ist ein fehlerhafter
Stage 0 daher eine irreversible Grenze des aktuellen Designs. Es wird keine
neue Bootloader-Updatearchitektur eingeführt und kein Debugger als Ersatzweg
vorausgesetzt.
