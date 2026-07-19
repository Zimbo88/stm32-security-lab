#!/usr/bin/env python3
import argparse
import binascii
import struct
from pathlib import Path

MAGIC = 0x31474D49
HEADER_VERSION = 1
HEADER_AREA_SIZE = 0x200
VECTOR_ADDRESS = 0x08008200

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("payload")
    parser.add_argument("output")
    parser.add_argument("--image-version", type=int, default=1)
    args = parser.parse_args()

    payload = Path(args.payload).read_bytes()
    crc = binascii.crc32(payload) & 0xFFFFFFFF

    header = struct.pack(
        "<8I",
        MAGIC,
        HEADER_VERSION,
        args.image_version,
        VECTOR_ADDRESS,
        len(payload),
        crc,
        0,
        0,
    )

    image = header + bytes([0xFF]) * (HEADER_AREA_SIZE - len(header)) + payload
    Path(args.output).write_bytes(image)

    print(f"Payload size: {len(payload)} bytes")
    print(f"CRC32:        0x{crc:08X}")
    print(f"Image size:   {len(image)} bytes")
    print(f"Output:       {args.output}")

if __name__ == "__main__":
    main()
