#!/usr/bin/env bash
set -euo pipefail

LAB="${HOME}/stm32-security-lab"
SRC_BOOT="${LAB}/firmware/exp019_signed_bootloader"
DST_BOOT="${LAB}/firmware/exp022_rollback_floor"
SRC_IMAGE="${LAB}/signing/exp018/exp018_signed_application_image.bin"
PRIVATE_KEY="${LAB}/signing/exp016/firmware_signing_private.pem"
PUBLIC_KEY="${LAB}/signing/exp016/firmware_signing_public.pem"
OUT="${LAB}/signing/exp022"

for f in "${SRC_BOOT}/Makefile" "${SRC_IMAGE}" "${PRIVATE_KEY}" "${PUBLIC_KEY}"; do
    if [[ ! -f "${f}" ]]; then
        echo "FEHLER: Datei fehlt: ${f}" >&2
        exit 1
    fi
done

rm -rf "${DST_BOOT}"
cp -a "${SRC_BOOT}" "${DST_BOOT}"
rm -rf "${DST_BOOT}/build"
mkdir -p "${OUT}"

sed -i 's/PROJECT := exp019_signed_bootloader/PROJECT := exp022_rollback_floor/' \
    "${DST_BOOT}/Makefile"

python3 - "${DST_BOOT}/src/signed_image.h" "${DST_BOOT}/src/signed_image.c" "${DST_BOOT}/src/main.c" <<'PY'
from pathlib import Path
import sys

h = Path(sys.argv[1])
c = Path(sys.argv[2])
m = Path(sys.argv[3])

text = h.read_text()
text = text.replace(
    "#define SIGNED_HEADER_VERSION  1UL\n",
    "#define SIGNED_HEADER_VERSION  1UL\n#define MIN_IMAGE_VERSION      2UL\n"
)
text = text.replace(
    "    VERIFY_BAD_HEADER_VERSION,\n",
    "    VERIFY_BAD_HEADER_VERSION,\n    VERIFY_ROLLBACK_VERSION,\n"
)
h.write_text(text)

text = c.read_text()
needle = """    if (m->header_version != SIGNED_HEADER_VERSION) {
        return VERIFY_BAD_HEADER_VERSION;
    }

"""
replacement = needle + """    if (m->image_version < MIN_IMAGE_VERSION) {
        return VERIFY_ROLLBACK_VERSION;
    }

"""
if needle not in text:
    raise SystemExit("Header-Version-Prüfung nicht gefunden")
text = text.replace(needle, replacement)

needle = '    case VERIFY_BAD_HEADER_VERSION: return "BAD HEADER VERSION";\n'
replacement = needle + '    case VERIFY_ROLLBACK_VERSION:   return "ROLLBACK VERSION REJECTED";\n'
if needle not in text:
    raise SystemExit("Status-Text-Stelle nicht gefunden")
text = text.replace(needle, replacement)
c.write_text(text)

text = m.read_text()
needle = """    uart_puts("\\nImage version    = ");
    uart_put_u32(m->image_version);
"""
replacement = """    uart_puts("\\nImage version    = ");
    uart_put_u32(m->image_version);
    uart_puts("\\nMinimum version  = ");
    uart_put_u32(MIN_IMAGE_VERSION);
"""
if needle not in text:
    raise SystemExit("Image-Version-Ausgabe nicht gefunden")
text = text.replace(needle, replacement)
m.write_text(text)
PY

python3 - "${SRC_IMAGE}" "${OUT}/manifest_v2.bin" "${OUT}/payload.bin" <<'PY'
from pathlib import Path
import struct
import sys

image = Path(sys.argv[1]).read_bytes()
manifest_path = Path(sys.argv[2])
payload_path = Path(sys.argv[3])

manifest = bytearray(image[:96])
payload = image[0x200:]

old_version = struct.unpack_from("<I", manifest, 0x08)[0]
struct.pack_into("<I", manifest, 0x08, 2)

manifest_path.write_bytes(manifest)
payload_path.write_bytes(payload)

print(f"Alte Version: {old_version}")
print("Neue Version: 2")
print(f"Payload:      {len(payload)} Bytes")
PY

openssl pkeyutl \
    -sign \
    -rawin \
    -inkey "${PRIVATE_KEY}" \
    -in "${OUT}/manifest_v2.bin" \
    -out "${OUT}/manifest_v2.sig"

openssl pkeyutl \
    -verify \
    -rawin \
    -pubin \
    -inkey "${PUBLIC_KEY}" \
    -in "${OUT}/manifest_v2.bin" \
    -sigfile "${OUT}/manifest_v2.sig"

python3 - "${OUT}/manifest_v2.bin" "${OUT}/manifest_v2.sig" "${OUT}/payload.bin" "${OUT}/signed_image_v2.bin" <<'PY'
from pathlib import Path
import hashlib
import struct
import sys

manifest = Path(sys.argv[1]).read_bytes()
signature = Path(sys.argv[2]).read_bytes()
payload = Path(sys.argv[3]).read_bytes()
out = Path(sys.argv[4])

if len(manifest) != 96:
    raise SystemExit("Manifest muss 96 Byte groß sein")
if len(signature) != 64:
    raise SystemExit("Signatur muss 64 Byte groß sein")

stored_hash = manifest[32:96]
actual_hash = hashlib.sha512(payload).digest()
if stored_hash != actual_hash:
    raise SystemExit("Payload-Hash stimmt nicht")

version = struct.unpack_from("<I", manifest, 0x08)[0]
if version != 2:
    raise SystemExit("Version ist nicht 2")

header = manifest + signature
header += b"\xff" * (0x200 - len(header))
image = header + payload
out.write_bytes(image)

print(f"V2-Image: {out}")
print(f"Größe:    {len(image)} Bytes")
PY

sha256sum \
    "${SRC_IMAGE}" \
    "${OUT}/signed_image_v2.bin" \
    | tee "${OUT}/images.sha256"

cat > "${DST_BOOT}/README.md" <<'EOF'
# EXP022 – Rollback floor demonstration

This bootloader adds a compiled minimum accepted image version:

- `MIN_IMAGE_VERSION = 2`
- correctly signed image version 1: rejected
- correctly signed image version 2: accepted

This is a laboratory policy demonstration, not complete production rollback
protection. An attacker who can replace the bootloader could restore an older
minimum-version policy. Production designs need protected immutable boot code
and trusted monotonic state.
EOF

cat > "${OUT}/results.txt" <<EOF
EXP022 ROLLBACK TEST PREPARATION

Bootloader:
- Project: firmware/exp022_rollback_floor
- Minimum accepted image version: 2

Test images:
- Old correctly signed image version 1:
  signing/exp018/exp018_signed_application_image.bin
- New correctly signed image version 2:
  signing/exp022/signed_image_v2.bin

Expected:
- Version 1 -> ROLLBACK VERSION REJECTED
- Version 2 -> Verification OK
EOF

echo
echo "Projekt: ${DST_BOOT}"
echo "V2-Image: ${OUT}/signed_image_v2.bin"
echo
echo "Nächster Schritt:"
echo "  cd ${DST_BOOT}"
echo "  make clean && make"
echo
echo "Noch nichts flashen."
