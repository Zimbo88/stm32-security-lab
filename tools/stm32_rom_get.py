#!/usr/bin/env python3
import argparse
import sys
import time

import serial

ACK = 0x79
NACK = 0x1F

def read_exact(ser: serial.Serial, count: int) -> bytes:
    data = ser.read(count)
    if len(data) != count:
        raise RuntimeError(
            f"Timeout: expected {count} bytes, received {len(data)}: {data.hex(' ')}"
        )
    return data

def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="/dev/ttyUSB0")
    parser.add_argument("--baud", type=int, default=115200)
    args = parser.parse_args()

    with serial.Serial(
        port=args.port,
        baudrate=args.baud,
        bytesize=serial.EIGHTBITS,
        parity=serial.PARITY_EVEN,
        stopbits=serial.STOPBITS_ONE,
        timeout=2.0,
        write_timeout=2.0,
        xonxoff=False,
        rtscts=False,
        dsrdtr=False,
    ) as ser:
        ser.reset_input_buffer()
        ser.reset_output_buffer()
        time.sleep(0.05)

        ser.write(b"\x7f")
        ser.flush()
        sync = read_exact(ser, 1)[0]
        print(f"Sync-Antwort: 0x{sync:02X}")

        if sync == NACK:
            raise RuntimeError(
                "NACK auf 0x7F: Bootloader war wahrscheinlich bereits synchronisiert. "
                "Board neu einschalten oder RESET drücken und Skript genau einmal starten."
            )
        if sync != ACK:
            raise RuntimeError(f"Unerwartete Sync-Antwort 0x{sync:02X}")

        ser.write(bytes((0x00, 0xFF)))
        ser.flush()

        get_ack = read_exact(ser, 1)[0]
        print(f"GET-Antwort:  0x{get_ack:02X}")
        if get_ack != ACK:
            raise RuntimeError(f"Kein ACK auf GET, Antwort 0x{get_ack:02X}")

        n = read_exact(ser, 1)[0]
        payload = read_exact(ser, n + 1)
        final_ack = read_exact(ser, 1)[0]

        print(f"N-Feld:             {n}")
        print(f"Bootloader-Version: 0x{payload[0]:02X}")
        print("Unterstützte Befehle:")
        for command in payload[1:]:
            print(f"  0x{command:02X}")
        print(f"Abschluss-ACK:      0x{final_ack:02X}")

        return 0

if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (serial.SerialException, RuntimeError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
