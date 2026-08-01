#!/usr/bin/env python3
"""Reset the connected ESP32 and capture its boot serial output.

The firmware prints once during setup(), so a reset is required to see it.
The script pulses the board's EN pin (via RTS) to trigger a clean reboot,
then reads everything the board sends on the serial line.

Usage:
    python scripts/serial_reset.py [--port /dev/ttyUSB0] [--baud 115200] [--seconds 8]
"""

import argparse
import sys
import time

import serial


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="/dev/ttyUSB0", help="Serial device (default: /dev/ttyUSB0)")
    parser.add_argument("--baud", type=int, default=115200, help="Baud rate (default: 115200)")
    parser.add_argument("--seconds", type=float, default=8.0, help="Seconds to capture (default: 8)")
    args = parser.parse_args()

    ser = serial.Serial(args.port, args.baud, timeout=1)

    # Reset sequence for the classic ESP32 DevKitC auto-download circuit.
    # RTS drives the transistor controlling EN; pulsing it reboots the chip.
    ser.setDTR(False)
    time.sleep(0.05)
    ser.setRTS(True)   # EN pulled low  -> reset asserted
    time.sleep(0.1)
    ser.setRTS(False)  # EN released    -> chip boots
    ser.reset_input_buffer()

    end = time.time() + args.seconds
    captured = 0
    while time.time() < end:
        data = ser.read(4096)
        if data:
            captured += len(data)
            sys.stdout.write(data.decode("utf-8", errors="replace"))
            sys.stdout.flush()

    ser.close()
    print(f"\n--- captured {captured} bytes ---")


if __name__ == "__main__":
    main()
