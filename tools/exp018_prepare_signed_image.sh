#!/usr/bin/env bash
set -euo pipefail

LAB="${HOME}/stm32-security-lab"
PAYLOAD="${LAB}/firmware/exp014_application_crc/build/exp014_application_crc_payload.bin"
PRIVATE_KEY="${LAB}/signing/exp016/firmware_signing_private.pem"
PUBLIC_KEY="${LAB}/signing/exp016/firmware_signing_public.pem"
OUT="${LAB}/signing/exp018"

MANIFEST="${OUT}/manifest.bin"
SIGNATURE="${OUT}/manifest.sig"
HEADER="${OUT}/signed_header.bin"
IMAGE="${OUT}/exp018_signed_application_image.bin"
TAMPERED="${OUT}/exp018_signed_application_image_tampered.bin"

mkdir -p "${OUT}"

for file in "${PAYLOAD}" "${PRIVATE_KEY}" "${PUBLIC_KEY}"; do
    if [[ ! -f "${file}" ]]; then
        echo "ERROR: File is missing: ${file}" >&2
        exit 1
    fi
done

echo "== EXP018: signiertes Imageformat vorbereiten =="
echo "Payload: ${PAYLOAD}"

python3 - "${PAYLOAD}" "${MANIFEST}" <<'PY'
from pathlib import Path
import hashlib
import struct
import sys

payload_path = Path(sys.argv[1])
manifest_path = Path(sys.argv[2])

MAGIC = 0x31474953          # little endian bytes: "SIG1"
HEADER_VERSION = 1
IMAGE_VERSION = 1
VECTOR_ADDRESS = 0x08008200
FLAGS = 0
RESERVED0 = 0

payload = payload_path.read_bytes()
payload_hash = hashlib.sha512(payload).digest()

# Exactly 96 bytes:
# 8 little-endian uint32 fields (32 bytes), followed by SHA-512 (64 bytes).
manifest = struct.pack(
    "<8I",
    MAGIC,
    HEADER_VERSION,
    IMAGE_VERSION,
    VECTOR_ADDRESS,
    len(payload),
    FLAGS,
    RESERVED0,
    RESERVED0,
) + payload_hash

if len(manifest) != 96:
    raise SystemExit(f"Manifest hat {len(manifest)} statt 96 Bytes")

manifest_path.write_bytes(manifest)

print(f"Payload-Größe:    {len(payload)} Bytes")
print(f"Payload-SHA512:   {payload_hash.hex()}")
print(f"Manifest-Größe:   {len(manifest)} Bytes")
print(f"Magic:            0x{MAGIC:08X} (SIG1)")
print(f"Vector-Adresse:   0x{VECTOR_ADDRESS:08X}")
PY

echo
echo "== 96-Byte-Manifest mit Ed25519 signieren =="
openssl pkeyutl \
    -sign \
    -rawin \
    -inkey "${PRIVATE_KEY}" \
    -in "${MANIFEST}" \
    -out "${SIGNATURE}"

if [[ "$(stat -c %s "${SIGNATURE}")" -ne 64 ]]; then
    echo "ERROR: Ed25519 signature is not 64 bytes." >&2
    exit 1
fi

echo "Signatur-Größe:   64 Bytes"

echo
echo "== Signatur unabhängig prüfen =="
openssl pkeyutl \
    -verify \
    -rawin \
    -pubin \
    -inkey "${PUBLIC_KEY}" \
    -in "${MANIFEST}" \
    -sigfile "${SIGNATURE}"

python3 - "${MANIFEST}" "${SIGNATURE}" "${PAYLOAD}" "${HEADER}" "${IMAGE}" <<'PY'
from pathlib import Path
import sys

manifest_path = Path(sys.argv[1])
signature_path = Path(sys.argv[2])
payload_path = Path(sys.argv[3])
header_path = Path(sys.argv[4])
image_path = Path(sys.argv[5])

HEADER_AREA_SIZE = 0x200

manifest = manifest_path.read_bytes()
signature = signature_path.read_bytes()
payload = payload_path.read_bytes()

if len(manifest) != 96:
    raise SystemExit("Manifest-Länge ungültig")
if len(signature) != 64:
    raise SystemExit("Signatur-Länge ungültig")

# Layout:
# 0x000..0x05F manifest (96 bytes)
# 0x060..0x09F signature (64 bytes)
# 0x0A0..0x1FF reserved/padding
# 0x200..       payload, whose vector table is therefore at 0x08008200
header = manifest + signature
header += bytes([0xFF]) * (HEADER_AREA_SIZE - len(header))

if len(header) != HEADER_AREA_SIZE:
    raise SystemExit("Header-Länge ungültig")

image = header + payload
header_path.write_bytes(header)
image_path.write_bytes(image)

print(f"Header-Größe:     {len(header)} Bytes")
print(f"Gesamtimage:      {len(image)} Bytes")
print(f"Payload-Offset:   0x{HEADER_AREA_SIZE:03X}")
print(f"Signatur-Offset:  0x060")
PY

echo
echo "== Host-Verifikation des fertigen Images =="
python3 - "${IMAGE}" "${OUT}/extracted_manifest.bin" "${OUT}/extracted_signature.bin" "${OUT}/extracted_payload.bin" <<'PY'
from pathlib import Path
import hashlib
import struct
import sys

image = Path(sys.argv[1]).read_bytes()
manifest_path = Path(sys.argv[2])
signature_path = Path(sys.argv[3])
payload_path = Path(sys.argv[4])

manifest = image[0:96]
signature = image[96:160]
payload = image[0x200:]

(
    magic,
    header_version,
    image_version,
    vector_address,
    image_size,
    flags,
    reserved0,
    reserved1,
) = struct.unpack("<8I", manifest[:32])

stored_hash = manifest[32:96]
computed_hash = hashlib.sha512(payload).digest()

print(f"Magic:            0x{magic:08X}")
print(f"Header-Version:   {header_version}")
print(f"Image-Version:    {image_version}")
print(f"Vector-Adresse:   0x{vector_address:08X}")
print(f"Image-Größe:      {image_size}")
print(f"Payload vorhanden:{len(payload)}")
print(f"SHA512 stimmt:    {stored_hash == computed_hash}")

if magic != 0x31474953:
    raise SystemExit("Magic ungültig")
if header_version != 1:
    raise SystemExit("Header-Version ungültig")
if vector_address != 0x08008200:
    raise SystemExit("Vektoradresse ungültig")
if image_size != len(payload):
    raise SystemExit("Image-Größe stimmt nicht")
if stored_hash != computed_hash:
    raise SystemExit("SHA-512 stimmt nicht")

manifest_path.write_bytes(manifest)
signature_path.write_bytes(signature)
payload_path.write_bytes(payload)
PY

openssl pkeyutl \
    -verify \
    -rawin \
    -pubin \
    -inkey "${PUBLIC_KEY}" \
    -in "${OUT}/extracted_manifest.bin" \
    -sigfile "${OUT}/extracted_signature.bin"

echo
echo "== Negativtest: Ein Payload-Byte verändern =="
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

print(f"Offset:           0x{offset:04X}")
print(f"Original:         0x{before:02X}")
print(f"Manipuliert:      0x{data[offset]:02X}")
PY

python3 - "${TAMPERED}" <<'PY'
from pathlib import Path
import hashlib
import sys

image = Path(sys.argv[1]).read_bytes()
stored_hash = image[32:96]
payload = image[0x200:]
computed_hash = hashlib.sha512(payload).digest()

print(f"Manipulationsprüfung – SHA512 stimmt: {stored_hash == computed_hash}")

if stored_hash == computed_hash:
    raise SystemExit("ERROR: Modification was not detected")
PY

sha256sum \
    "${MANIFEST}" \
    "${SIGNATURE}" \
    "${HEADER}" \
    "${IMAGE}" \
    | tee "${OUT}/artifacts.sha256"

cat > "${OUT}/results.txt" <<EOF
EXP018 SIGNED IMAGE FORMAT – HOST VALIDATION

Image layout:
- 0x000..0x05F: 96-byte signed manifest
- 0x060..0x09F: 64-byte Ed25519 signature
- 0x0A0..0x1FF: reserved padding
- 0x200 onward: application payload
- Flash image base: 0x08008000
- Application vector address: 0x08008200

Signed manifest:
- Fixed metadata: 32 bytes
- Payload SHA-512: 64 bytes
- Total: 96 bytes

Results:
- Manifest signature verification: PASS
- Payload SHA-512 verification: PASS
- One-byte payload modification: REJECTED by SHA-512 comparison

No STM32 flash was modified by this experiment.
EOF

echo
echo "PASS: EXP018 image format was validated successfully on the host."
echo "Image: ${IMAGE}"
echo "Noch nichts auf den STM32 flashen."
