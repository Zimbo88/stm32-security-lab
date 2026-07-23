# Secure Update Streaming Installer Design

Stand: implementierte Architektur. Dieser Text beschreibt die Erweiterung des
bestehenden Update-Installers um eine zustandsbehaftete Streaming-API. Die
bestehende `update_installer_install()`-API bleibt erhalten und ist als
Kompatibilitaetswrapper ueber diese API implementierbar.

## Ziele

- Das vollstaendige Update-Paket liegt nie im RAM.
- Es gibt keine dynamische Speicherallokation.
- Der Aufrufer stellt kleine feste Program- und Readback-Puffer bereit.
- Das bestehende Paketformat bleibt kompatibel:
  `manifest || signature || header-padding || payload`.
- Manifest und Signatur werden vor dem Loeschen des Candidate-Slots geprueft.
- Die Ed25519-Pruefung bleibt erhalten.
- Rollback Protection greift vor dem ersten Flash-Schreibvorgang.
- Der inaktive Slot wird automatisch aus den Boot-Metadaten bestimmt.
- Die Metadatenzustaende `WRITING` und `CANDIDATE_READY` bleiben erhalten.
- Ein Reset oder Stromausfall waehrend `begin`, erase, write oder `finish`
  darf kein teilweise geschriebenes Image bootfaehig machen.
- Die bestehende direkte Flash-Verifikation nach dem Schreiben bleibt
  erhalten.

## Bestehender Ausgangspunkt

Der aktuelle Installer fuehrt `update_installer_install()` in einer einzigen
Operation aus:

1. Optionen und Package-Puffer validieren.
2. Boot-Metadaten recovern.
3. Inaktiven Slot bestimmen.
4. Vollstaendiges Paket mit `update_package_verify_for_slot()` vorverifizieren.
5. Rollback gegen die aktuelle Metadaten-Version pruefen.
6. Metadaten auf `WRITING` setzen.
7. Candidate-Slot loeschen.
8. Paket mit Program-Puffer in den Candidate-Slot schreiben und pro Block
   zuruecklesen.
9. Payload aus Flash mit `hash_installed_payload()` hashen.
10. Installiertes Image direkt aus dem memory-mapped Candidate-Slot mit
    `signed_image_verify_update_slot_buffer()` verifizieren.
11. Metadaten auf `CANDIDATE_READY` setzen.

Die Streaming-API muss diese Sequenz in explizite Phasen aufteilen. Der
wichtige Unterschied: Vor dem Loeschen steht nur der Header zur Verfuegung,
nicht der gesamte Payload. Daher sind vor dem Loeschen nur Header-,
Manifest-, Signatur-, Slot-, Groessen- und Rollback-Pruefungen moeglich. Der
Payload-Hash, die Vector-Table-Pruefung und die finale Slot-Verifikation
erfolgen nach bzw. waehrend der Payload-Uebertragung.

## Vorgeschlagene API

Die Namen folgen dem bestehenden `update_installer_*`-Praefix.

```c
typedef enum {
    UPDATE_INSTALL_SESSION_EMPTY = 0,
    UPDATE_INSTALL_SESSION_INITIALIZED,
    UPDATE_INSTALL_SESSION_WRITING,
    UPDATE_INSTALL_SESSION_PAYLOAD_COMPLETE,
    UPDATE_INSTALL_SESSION_FINISHED,
    UPDATE_INSTALL_SESSION_ABORTED,
    UPDATE_INSTALL_SESSION_FAILED
} update_installer_session_state_t;

typedef struct {
    signed_manifest_t manifest;
    uint8_t manifest_bytes[SIGNED_MANIFEST_SIZE];
    uint8_t signature_bytes[SIGNED_SIGNATURE_SIZE];
    size_t payload_size;
    size_t package_size;
} update_package_header_t;

typedef struct {
    update_installer_session_state_t state;

    boot_flash_t restricted_flash;
    boot_flash_region_t write_regions[3];

    const boot_flash_t *flash;
    const uint8_t *public_key;
    update_install_options_t options;
    update_install_result_t *result;

    boot_metadata_record_t metadata_before;
    boot_metadata_record_t writing_metadata;
    boot_metadata_recovery_t metadata_recovery;

    const boot_slot_descriptor_t *active;
    const boot_slot_descriptor_t *candidate;

    update_package_header_t header;
    crypto_sha512_ctx payload_hash_ctx;

    size_t payload_received;
    size_t payload_programmed;
    size_t payload_program_size;
    size_t program_fill;
    uint32_t programmed_block_count;

    uint8_t metadata_writing_committed;
    uint8_t candidate_erased;
    uint8_t payload_hash_finalized;
} update_installer_session_t;

update_install_status_t update_installer_session_init(
    update_installer_session_t *session,
    const boot_flash_t *flash,
    const uint8_t public_key[FIRMWARE_PUBLIC_KEY_SIZE],
    const update_install_options_t *options,
    update_install_result_t *result
);

update_install_status_t update_installer_begin(
    update_installer_session_t *session,
    const uint8_t header[SIGNED_IMAGE_HEADER_SIZE],
    size_t header_size
);

update_install_status_t update_installer_write(
    update_installer_session_t *session,
    size_t payload_offset,
    const uint8_t *data,
    size_t length
);

update_install_status_t update_installer_finish(
    update_installer_session_t *session
);

update_install_status_t update_installer_abort(
    update_installer_session_t *session
);
```

`update_installer_session_t` ist aufruferallokiert. Die grossen I/O-Puffer
bleiben in `update_install_options_t`. Dadurch braucht die API keinen
vollstaendigen Paketpuffer, keine Heap-Allokation und keinen grossen Stack.

Bestehende Statuswerte sollten nicht umnummeriert werden. Falls neue Fehler
noetig sind, werden sie hinten angehaengt, zum Beispiel:

- `UPDATE_INSTALL_ERR_STATE` fuer eine falsche API-Reihenfolge.
- `UPDATE_INSTALL_ERR_SEQUENCE` fuer wiederholte, uebersprungene oder
  ueberlange Payload-Bloecke.

## Header-Parsing und Signaturpruefung

Fuer Streaming wird ein header-only Parser benoetigt. Er sollte in
`update_package.c` liegen, damit Paketformatregeln nicht im Installer
dupliziert werden.

Implementierte Hilfsfunktion:

```c
update_package_status_t update_package_verify_header_for_slot(
    const uint8_t header[SIGNED_IMAGE_HEADER_SIZE],
    size_t header_size,
    const uint8_t public_key[FIRMWARE_PUBLIC_KEY_SIZE],
    const boot_slot_descriptor_t *slot,
    update_package_header_t *parsed,
    verify_status_t *verify_status
);
```

Diese Funktion prueft vor dem Loeschen des Candidate-Slots:

- `header_size == SIGNED_IMAGE_HEADER_SIZE`.
- Manifest kann dekodiert werden.
- Magic, Header-Version, Flags, Target Compatibility und Image Type passen.
- `image_size >= APPLICATION_MIN_SIZE`.
- `image_size <= slot->maximum_payload_size`.
- `vector_address == slot->payload_base`.
- `SIGNED_IMAGE_HEADER_SIZE + image_size` laeuft nicht ueber.
- Die auf die Flash-Programmierausrichtung gerundete Groesse passt in den Slot.
- Header-Padding zwischen Signatur und Payload ist vollstaendig `0xFF`.
- Ed25519-Signatur ist gueltig:
  `crypto_ed25519_check(signature, public_key, manifest_bytes,
  SIGNED_MANIFEST_SIZE)`.

Diese Funktion prueft noch nicht:

- Payload-Hash.
- Vector Table im Payload.
- Reset-Handler-Adresse im Payload.

Diese Daten sind zu diesem Zeitpunkt noch nicht vollstaendig vorhanden. Die
Sicherheit kommt daher aus der Kombination aus authentifiziertem Manifest,
Rollback-Pruefung vor dem Schreiben, `WRITING`-Metadaten und finaler
Flash-Verifikation vor `CANDIDATE_READY`.

## Sitzungsablauf

### `session_init`

`update_installer_session_init()`:

1. Nullpointer und `update_install_options_t` wie heute validieren.
2. Sicherstellen, dass `program_buffer_size` und `SIGNED_IMAGE_HEADER_SIZE`
   zur Flash-Programmierausrichtung passen.
3. Boot-Metadaten recovern.
4. Den aktiven Slot bestimmen.
5. Den inaktiven Candidate-Slot automatisch als Gegenstueck zum aktiven Slot
   waehlen.
6. Restricted Flash wie heute auf Metadatenbereiche und Candidate-Slot
   begrenzen.
7. Result-Struktur initialisieren.

Implementierter Startzustand:

- `CONFIRMED`: normaler Start.

Der Installer startet bewusst konservativ nur aus einem bestaetigten aktiven
Image. `WRITING` und `REJECTED_INVALID` bleiben Boot-seitig sicher, weil die
Slot-Auswahl auf den bestaetigten aktiven Slot zurueckfaellt; eine neue
Installer-Session bereinigt diese Zustaende aktuell aber nicht automatisch.
Sie liefert `UPDATE_INSTALL_ERR_ACTIVE_STATE`, bis eine separate Recovery- oder
Policy-Entscheidung den Metadatenzustand bereinigt.

Nicht zulaessig fuer eine neue Installation:

- `CANDIDATE_READY` und `PENDING_TRIAL`, weil ein bestehender Candidate noch
  vom Trial-/Confirmation-Flow verarbeitet werden muss.
- `EMPTY`, wenn kein aktiver Slot bekannt ist.
- `WRITING` und `REJECTED_INVALID`, solange keine explizite Recovery-Policy
  ausgefuehrt wurde.

### `begin`

`update_installer_begin()` nimmt exakt den 0x200-Byte-Header entgegen.

Reihenfolge:

1. Header-only Parser ausfuehren.
2. Rollback pruefen. Die neue `manifest.image_version` muss groesser als die
   aktuelle Rollback-Schwelle sein. In `CONFIRMED` ist das die aktuelle
   `candidate_image_version` der Metadaten, die dort die bestaetigte aktive
   Version repraesentiert.
3. Fault Hook `UPDATE_INSTALL_FAULT_METADATA_WRITING`.
4. Metadaten nach `WRITING` committen.
5. Metadaten erneut recovern und pruefen, dass `WRITING` sichtbar ist.
6. Fault Hooks und Erase pro Candidate-Sektor ausfuehren.
7. Den Header in den Candidate-Slot schreiben:
   - Manifest an `candidate->manifest_address`.
   - Signatur an `candidate->signature_address`.
   - Padding als bereits validierte `0xFF`-Bytes bis
     `candidate->payload_base`.
8. Jeden geschriebenen Header-Block per `readback_buffer` vergleichen.
9. SHA-512-Kontext fuer den Streaming-Payload initialisieren.
10. Session-State auf `WRITING` setzen.

Wenn der Strom vor dem `WRITING`-Commit ausfaellt, bleiben die alten
Metadaten erhalten. Wenn der Strom nach dem `WRITING`-Commit ausfaellt, ist
der Candidate nicht bootfaehig, weil Boot Slot Selection fuer `WRITING` auf
den bestaetigten aktiven Slot zurueckfaellt.

### `write`

`update_installer_write()` nimmt nur Payload-Daten entgegen. Header-Bytes
werden nie ueber `write()` verarbeitet.

Regeln:

- `session->state` muss `WRITING` sein.
- `data` darf bei `length > 0` nicht `NULL` sein.
- `payload_offset` muss exakt `session->payload_received` entsprechen.
- `length` muss groesser als 0 sein.
- `payload_received + length` darf `header.payload_size` nicht ueberschreiten.
- Die Funktion akzeptiert entweder den gesamten Block oder gar nichts.

Damit sind Wiederholungen und Luecken eindeutig:

- `payload_offset < payload_received`: wiederholter oder alter Block,
  `UPDATE_INSTALL_ERR_SEQUENCE`.
- `payload_offset > payload_received`: uebersprungener Block,
  `UPDATE_INSTALL_ERR_SEQUENCE`.
- `payload_offset == payload_received`, aber Block wuerde ueber
  `image_size` hinausreichen: ueberlanger Block, `UPDATE_INSTALL_ERR_SEQUENCE`
  oder `UPDATE_INSTALL_ERR_PACKAGE`.

Die Implementierung darf beliebige `length` verarbeiten, ohne den kompletten
Block zu puffern:

1. Daten in `options.program_buffer` sammeln.
2. Sobald `program_buffer` voll ist, an
   `candidate->payload_base + flash_payload_programmed` schreiben.
3. Den geschriebenen Block per `boot_flash_read()` in `readback_buffer`
   zuruecklesen und vergleichen.
4. Fault Hooks `UPDATE_INSTALL_FAULT_PROGRAM_BLOCK` und
   `UPDATE_INSTALL_FAULT_READBACK` pro programmiertem Block erhalten.
5. SHA-512 nur ueber die echten Payload-Bytes aktualisieren, niemals ueber
   0xFF-Padding.
6. `payload_received` exakt um die angenommene Laenge erhoehen.

Der letzte nicht ausgerichtete Rest bleibt bis `finish()` im Program-Puffer.
Er wird nicht gehasht, nachdem er mit 0xFF aufgefuellt wurde.

### `finish`

`update_installer_finish()` macht den Candidate nur dann bootfaehig, wenn alle
Schritte erfolgreich sind.

Reihenfolge:

1. `payload_received == header.payload_size` pruefen.
2. Den eventuell offenen Program-Puffer mit `0xFF` bis zur
   Flash-Programmierausrichtung auffuellen und programmieren.
3. Letzten Block per Readback vergleichen.
4. Streaming-SHA-512 finalisieren.
5. Streaming-Hash gegen `header.manifest.payload_sha512` vergleichen.
7. Bestehende Flash-Hash-Pruefung erhalten:
   Payload mit `boot_flash_read()` aus `candidate->payload_base` lesen und
   exakt `header.manifest.image_size` Bytes hashen. Die bestehenden Fault Hooks
   `UPDATE_INSTALL_FAULT_HASH_BEGIN` und
   `UPDATE_INSTALL_FAULT_HASH_COMPLETE` bleiben in dieser Flash-Readback-
   Pruefung erhalten.
8. Installierten Header aus Flash pruefen:
   - Manifest-Bytes aus `candidate->manifest_address` muessen den in der
     Session gespeicherten Manifest-Bytes entsprechen.
   - Signatur-Bytes aus `candidate->signature_address` muessen den in der
     Session gespeicherten Signatur-Bytes entsprechen.
   - Header-Padding in Flash muss weiterhin canonical `0xFF` sein.
9. Fault Hook `UPDATE_INSTALL_FAULT_VERIFY_INSTALLED`.
10. Direkt aus memory-mapped Flash verifizieren:

```c
signed_image_verify_update_slot_buffer(
    (const uint8_t *)(uintptr_t)candidate->manifest_address,
    (const uint8_t *)(uintptr_t)candidate->signature_address,
    (const uint8_t *)(uintptr_t)candidate->payload_base,
    (size_t)candidate->maximum_payload_size,
    public_key,
    candidate
);
```

`candidate->maximum_payload_size` ist hier nur die Kapazitaet. Die Funktion
darf nur `manifest.image_size` hashen. Das entspricht der aktuellen Semantik
von `signed_image_verify_update_slot_buffer()`: Die Kapazitaet wird als
Obergrenze und Range-Pruefung verwendet, nicht als tatsaechliche Payload-
Laenge.

11. Das von der direkten Flash-Verifikation dekodierte Manifest muss
    semantisch zur Session passen. Dadurch wird verhindert, dass ein anderes
    gueltig signiertes Image durch einen spaeten Flash-Eingriff als Ergebnis
    akzeptiert wird.
12. Fault Hook `UPDATE_INSTALL_FAULT_METADATA_CANDIDATE_READY`.
13. Metadaten nach `CANDIDATE_READY` committen.
14. Session-State auf `FINISHED` setzen.

Wenn irgendein Schritt vor `CANDIDATE_READY` fehlschlaegt, bleibt der
Candidate nicht bootfaehig. Die Session geht nach `FAILED`; der Aufrufer kann
`abort()` aufrufen oder neu starten.

### `abort`

`update_installer_abort()` ist sicherheitsorientiert, nicht rollback-frei.

- Vor einem erfolgreichen `WRITING`-Commit: keine Flash- oder Metadaten-
  Aenderung noetig; Session wird `ABORTED`.
- Nach einem erfolgreichen `WRITING`-Commit und vor `CANDIDATE_READY`:
  best-effort Metadaten-Transition nach `REJECTED_INVALID`.
- Wenn der Abort-Commit fehlschlaegt oder der Strom ausfaellt, bleibt der
  Zustand entweder `WRITING` oder wird `REJECTED_INVALID`. Beide Zustaende
  fuehren in der bestehenden Boot Slot Selection zum bestaetigten aktiven
  Slot, nicht zum Candidate.
- Nach `FINISHED` ist `abort()` nicht mehr zulaessig. Ein `CANDIDATE_READY`
  Image wird vom bestehenden Trial-/Confirmation-/Reject-Flow behandelt.

Der Candidate-Slot muss bei Abort nicht geloescht werden. Die Bootfaehigkeit
wird ausschliesslich ueber Metadaten plus finale Slot-Verifikation freigegeben.

## Sicherheitsinvarianten

- `CANDIDATE_READY` wird erst nach erfolgreichem Streaming-Hash,
  Flash-Readback-Hash, Header-Readback, direkter Slot-Verifikation und
  Manifest-Abgleich geschrieben.
- `WRITING` und `REJECTED_INVALID` sind nie Candidate-Bootzustaende.
- Vor dem ersten Erase gibt es eine gueltige Ed25519-Signatur ueber das
  Manifest.
- Vor dem ersten Erase wurde Rollback Protection ausgewertet.
- `image_size` stammt aus dem signierten Manifest und wird fuer alle
  Payload-Laengen verwendet.
- `maximum_payload_size` wird nur als Slot-Kapazitaet verwendet.
- Jeder Adressbereich wird mit gepruefter Addition berechnet.
- Keine Adresse darf ausserhalb
  `candidate->signed_image_base <= address < candidate->slot_end` liegen.
- SHA-512 wird im Streaming-Pfad nur ueber echte Payload-Bytes aktualisiert.
- Padding wird geschrieben und gelesen, aber nie in den Payload-Hash
  aufgenommen.
- Ein API-Aufruf akzeptiert einen Payload-Block vollstaendig oder gar nicht.
- Nach einem Flash-Fehler geht die Session nach `FAILED`; weitere Writes sind
  unzulaessig.

## Integer- und Bounds-Regeln

Alle Berechnungen mit Paket-, Payload- und Flash-Groessen muessen vor dem
Cast auf `uint32_t` geprueft werden:

- `SIGNED_IMAGE_HEADER_SIZE + image_size`.
- `align_up(SIGNED_IMAGE_HEADER_SIZE + image_size, program_alignment)`.
- `payload_base + flash_payload_programmed`.
- `payload_received + length`.
- `flash_payload_programmed + programmed_length`.

`program_alignment` muss eine Zweierpotenz sein. `program_buffer_size` muss
ein Vielfaches von `program_alignment` sein. `readback_buffer_size` muss
mindestens so gross wie `program_buffer_size` sein, damit ein programmierter
Block ohne Teilvergleiche geprueft werden kann.

## Memory-Mapped-Flash-Annahmen

Die finale direkte Verifikation liest Manifest, Signatur und Payload ueber die
Slot-Adressen:

- `candidate->manifest_address`
- `candidate->signature_address`
- `candidate->payload_base`
- `candidate->maximum_payload_size`

Das setzt voraus, dass diese Adressen auf der Zielplattform tatsaechlich als
memory-mapped Flash lesbar sind. Das ist identisch zur aktuellen Boot-Policy.
Host-Tests muessen weiterhin eine Speicherabbildung dieser Adressen bereit-
stellen oder synchronisieren.

Alle Program- und Readback-Schritte selbst sollten weiterhin die
`boot_flash_*`-Abstraktion nutzen. Nur die finale Boot-equivalente
Verifikation nutzt direkte Pointer.

## Fault Injection

Bestehende Fault-Injection-Punkte bleiben erhalten und werden den neuen Phasen
zugeordnet:

- `UPDATE_INSTALL_FAULT_METADATA_WRITING`: in `begin`, direkt vor dem
  `WRITING`-Commit.
- `UPDATE_INSTALL_FAULT_ERASE_SECTOR`: in `begin`, pro geloeschtem Sector.
- `UPDATE_INSTALL_FAULT_PROGRAM_BLOCK`: in `begin`, `write` und `finish`, pro
  geschriebenem Header-/Payload-Block.
- `UPDATE_INSTALL_FAULT_READBACK`: in `begin`, `write` und `finish`, pro
  Readback-Vergleich.
- `UPDATE_INSTALL_FAULT_HASH_BEGIN`: in `finish`, direkt vor der bestehenden
  Flash-Readback-Hash-Pruefung.
- `UPDATE_INSTALL_FAULT_HASH_COMPLETE`: in `finish`, nach der
  Flash-Readback-Hash-Berechnung und vor deren Hash-Vergleich.
- `UPDATE_INSTALL_FAULT_VERIFY_INSTALLED`: in `finish`, direkt vor der
  direkten memory-mapped Slot-Verifikation.
- `UPDATE_INSTALL_FAULT_METADATA_CANDIDATE_READY`: in `finish`, direkt vor
  dem Commit nach `CANDIDATE_READY`.

Sinnvolle Erweiterungen fuer spaetere Tests:

- `UPDATE_INSTALL_FAULT_HEADER_ACCEPTED`: nach Header-/Signaturpruefung, aber
  vor `WRITING`.
- `UPDATE_INSTALL_FAULT_ABORT_REJECT`: vor dem best-effort Commit nach
  `REJECTED_INVALID`.
- `UPDATE_INSTALL_FAULT_RECOVER_STALE_WRITING`: wenn `session_init` einen
  alten `WRITING`-Zustand nach `REJECTED_INVALID` ueberfuehrt.

Neue Fault-Punkte sollten hinten an das Enum angehaengt werden.

## One-Shot-Wrapper

`update_installer_install()` kann so ueber Streaming laufen:

1. `update_installer_session_init(...)`.
2. Zur Kompatibilitaet das vollstaendige Paket wie bisher mit
   `update_package_verify_for_slot()` vorverifizieren.
3. `update_installer_begin(..., package_bytes, SIGNED_IMAGE_HEADER_SIZE)`.
4. Payload ab `package_bytes + SIGNED_IMAGE_HEADER_SIZE` in Schleife an
   `update_installer_write()` geben.
5. `update_installer_finish(...)`.
6. Bei Fehler nach `begin` best-effort `update_installer_abort(...)`.

Der Wrapper braucht weiterhin einen vollstaendigen Paketpuffer, weil seine
bestehende Signatur ihn annimmt. Die Streaming-API selbst braucht ihn nicht.

Durch die vollstaendige Vorverifikation behaelt der One-Shot-Wrapper das alte
Verhalten fuer defekte, abgeschnittene oder mit Zusatzdaten versehene Pakete:
solche Fehler brechen weiterhin vor `WRITING` und vor jedem Candidate-Erase ab.
Der reine Streaming-Pfad kann den Payload-Hash erst in `finish` pruefen; dort
bleibt der Candidate bis zum erfolgreichen `CANDIDATE_READY` nicht bootfaehig.

## Auswirkungen auf Tests

Bestehende Tests fuer `update_installer_install()` sollten nach der Wrapper-
Umstellung weiterlaufen. Erwartete Anpassungen:

- Tests fuer `update_install_options_t` bleiben auf Program-/Readback-Puffer
  fokussiert.
- Blockzaehler muessen definieren, ob Header-Bloecke mitgezaehlt werden. Fuer
  Kompatibilitaet sollte `programmed_block_count` alle Flash-Programmierbloecke
  inklusive Header zaehlen.
- Tests fuer invalides Manifest, falsches Target, falschen Slot, schlechte
  Signatur, schlechtes Padding und Rollback muessen bestaetigen, dass vor
  `WRITING`/Erase abgebrochen wird.
- Tests fuer defekten Payload-Hash, defekte Vector Table und manipulierte
  Payload-Daten muessen bestaetigen, dass `finish` fehlschlaegt und keine
  `CANDIDATE_READY`-Metadaten entstehen.
- Neue Streaming-Sequenztests:
  - gueltiges Paket groesser als Program- und Readback-Puffer.
  - `write` mit wiederholtem Offset.
  - `write` mit uebersprungenem Offset.
  - `write` mit Block ueber `image_size`.
  - `finish` vor vollstaendigem Payload.
  - `abort` vor `begin`, nach `begin`, nach Teilwrite und nach Fehler.
  - Reset-/Fault-Injection bei `WRITING`, Erase, Program, Readback,
    Hash-Complete, Verify-Installed und `CANDIDATE_READY`.
  - Manipuliertes `image_size` im installierten Manifest vor finaler
    Verifikation.
  - Header-Padding-Korruption im Flash vor `finish`.

Host-Tests muessen die memory-mapped Flash-Abbildung synchron halten, weil
`finish` wie die Boot-Policy direkt aus Slot-Adressen verifiziert.

## Offene Einschraenkungen

- Streaming kann den Payload-Hash nicht vor dem Loeschen des Candidate-Slots
  pruefen. Das ist eine erwartete Folge der RAM-Anforderung.
- Streaming ist nicht als Resume-Protokoll nach Reset ausgelegt. Nach Reset
  bleibt ein unterbrochener Candidate nicht bootfaehig; ein neuer Versuch
  startet von vorne.
- Neue Installer-Sessions starten aktuell nur aus `CONFIRMED`. Stale
  `WRITING`- oder `REJECTED_INVALID`-Zustaende muessen durch eine separate
  Recovery-Policy bereinigt werden, bevor ein neuer Installationsversuch
  beginnt.
- Die API setzt weiterhin memory-mapped Flash fuer die finale Boot-equivalente
  Verifikation voraus.
