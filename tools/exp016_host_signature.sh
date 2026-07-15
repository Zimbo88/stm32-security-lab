#!/usr/bin/env bash
set -euo pipefail

LAB="${HOME}/stm32-security-lab"
IMAGE="${LAB}/firmware/exp014_application_crc/build/exp014_application_crc_image.bin"
OUT="${LAB}/signing/exp016"
PRIVATE_KEY="${OUT}/firmware_signing_private.pem"
PUBLIC_KEY="${OUT}/firmware_signing_public.pem"
SIGNATURE="${OUT}/exp014_application_crc_image.sig"
TAMPERED="${OUT}/exp014_application_crc_image_tampered.bin"

mkdir -p "${OUT}"

if [[ ! -f "${IMAGE}" ]]; then
    echo "FEHLER: Image nicht gefunden: ${IMAGE}" >&2
    exit 1
fi

echo "== OpenSSL =="
openssl version

if [[ ! -f "${PRIVATE_KEY}" ]]; then
    echo
    echo "== Ed25519-Schlüsselpaar erzeugen =="
    umask 077
    openssl genpkey -algorithm ED25519 -out "${PRIVATE_KEY}"
    openssl pkey -in "${PRIVATE_KEY}" -pubout -out "${PUBLIC_KEY}"
else
    echo
    echo "Vorhandenen privaten Schlüssel verwenden: ${PRIVATE_KEY}"
    if [[ ! -f "${PUBLIC_KEY}" ]]; then
        openssl pkey -in "${PRIVATE_KEY}" -pubout -out "${PUBLIC_KEY}"
    fi
fi

chmod 600 "${PRIVATE_KEY}"
chmod 644 "${PUBLIC_KEY}"

echo
echo "== Image-Hashes =="
sha256sum "${IMAGE}" | tee "${OUT}/image.sha256"
sha256sum "${PUBLIC_KEY}" | tee "${OUT}/public_key.sha256"

echo
echo "== Image signieren =="
openssl pkeyutl \
    -sign \
    -rawin \
    -inkey "${PRIVATE_KEY}" \
    -in "${IMAGE}" \
    -out "${SIGNATURE}"

echo "Signaturgröße: $(stat -c %s "${SIGNATURE}") Bytes"
xxd -g 1 "${SIGNATURE}" | head

echo
echo "== Originalsignatur prüfen =="
openssl pkeyutl \
    -verify \
    -rawin \
    -pubin \
    -inkey "${PUBLIC_KEY}" \
    -in "${IMAGE}" \
    -sigfile "${SIGNATURE}"

echo
echo "== Manipulierte Kopie erzeugen =="
python3 - "${IMAGE}" "${TAMPERED}" <<'PY'
from pathlib import Path
import sys

src = Path(sys.argv[1])
dst = Path(sys.argv[2])
data = bytearray(src.read_bytes())

offset = 0x220
before = data[offset]
data[offset] ^= 0x01
dst.write_bytes(data)

print(f"Offset:      0x{offset:04X}")
print(f"Original:    0x{before:02X}")
print(f"Manipuliert: 0x{data[offset]:02X}")
PY

echo
echo "== Manipuliertes Image muss abgelehnt werden =="
set +e
openssl pkeyutl \
    -verify \
    -rawin \
    -pubin \
    -inkey "${PUBLIC_KEY}" \
    -in "${TAMPERED}" \
    -sigfile "${SIGNATURE}"
rc=$?
set -e

if [[ ${rc} -eq 0 ]]; then
    echo "FEHLER: Manipuliertes Image wurde unerwartet akzeptiert." >&2
    exit 1
fi

echo "PASS: Manipuliertes Image wurde von der Signaturprüfung abgelehnt."

cat > "${OUT}/results.txt" <<EOF
EXP016 HOST-SIDE SIGNATURE BASELINE

Algorithm:
- Ed25519

Signed object:
- ${IMAGE}

Artifacts:
- Private key: ${PRIVATE_KEY}
- Public key: ${PUBLIC_KEY}
- Signature: ${SIGNATURE}
- Tampered image: ${TAMPERED}

Results:
- Original image signature: PASS
- One-byte modified image signature: REJECTED

Important:
- This experiment verifies signatures on the host only.
- The STM32 bootloader still enforces CRC32 only.
- The private key must never be embedded in firmware or committed to Git.
EOF

echo
echo "Ergebnisse: ${OUT}/results.txt"
echo "WICHTIG: Privaten Schlüssel nicht zu Git hinzufügen."
