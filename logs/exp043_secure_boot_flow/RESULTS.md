# EXP043 – Secure Boot Flow Reconstruction

## Bootablauf

1. Reset startet den Bootloader bei 0x08000000.
2. UART is initialized.
3. The manifest at 0x08008000 is read.
4. signed_image_verify() prüft das Image.
5. On failure, the bootloader remains in a safe halt state.
6. Control is transferred to the application only after VERIFY_OK.

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
- SysTick is disabled.
- NVIC-Interrupts werden deaktiviert.
- Pending-Interrupts werden gelöscht.
- VTOR is set to 0x08008200.
- MSP is loaded from the application vector table.
- Interrupts werden wieder aktiviert.
- The application reset handler is called.

## Bewertung

- Fail-closed-Verhalten ist vorhanden.
- Payload-Integrität und Manifest-Authentizität werden getrennt geprüft.
- Rollback-Schutz basiert derzeit auf einem einkompilierten Mindestwert.
- Es existiert noch kein authentifizierter Wartungs- oder Recovery-Modus.
- Kein versteckter oder unauthentifizierter Speicherzugang ist vorgesehen.

## Status

EXP043 was performed entirely offline.
