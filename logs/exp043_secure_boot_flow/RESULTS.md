# EXP043 – Secure Boot Flow Reconstruction

## Bootablauf

1. Reset startet den Bootloader bei 0x08000000.
2. UART wird initialisiert.
3. Das Manifest bei 0x08008000 wird gelesen.
4. signed_image_verify() prüft das Image.
5. Bei einem Fehler bleibt der Bootloader sicher stehen.
6. Nur bei VERIFY_OK wird zur Anwendung gesprungen.

## Verifikationsreihenfolge

1. Manifest-Strukturgröße
2. Magic-Wert
3. Header-Version
4. Mindest-Image-Version
5. Vektoradresse
6. Imagegröße
7. Initialer MSP
8. Reset-Vektor
9. SHA-512 des Payloads
10. Ed25519-Signatur des Manifests

## Übergabe an die Anwendung

- Interrupts werden global deaktiviert.
- SysTick wird deaktiviert.
- NVIC-Interrupts werden deaktiviert.
- Pending-Interrupts werden gelöscht.
- VTOR wird auf 0x08008200 gesetzt.
- MSP wird aus der Anwendung geladen.
- Interrupts werden wieder aktiviert.
- Der Reset-Handler der Anwendung wird aufgerufen.

## Bewertung

- Fail-closed-Verhalten ist vorhanden.
- Payload-Integrität und Manifest-Authentizität werden getrennt geprüft.
- Rollback-Schutz basiert derzeit auf einem einkompilierten Mindestwert.
- Es existiert noch kein authentifizierter Wartungs- oder Recovery-Modus.
- Kein versteckter oder unauthentifizierter Speicherzugang ist vorgesehen.

## Status

EXP043 wurde vollständig offline durchgeführt.
