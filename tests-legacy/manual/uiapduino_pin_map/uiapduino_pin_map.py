#!/usr/bin/env python3
# /// script
# requires-python = ">=3.9"
# dependencies = ["pyserial>=3.5"]
# ///
"""Capture two UIAPduino pin-map cycles from the E132 ESP32 fixture."""

import argparse
import time

import serial


def main(port: str, seconds: float):
    with serial.Serial(port, 115200, timeout=0.2) as uart:
        time.sleep(2.0)
        uart.reset_input_buffer()
        uart.write(b"G")
        deadline = time.monotonic() + seconds
        saw_end = False
        while time.monotonic() < deadline:
            line = uart.readline().decode("utf-8", "replace").strip()
            if not line:
                continue
            print(line)
            if line == "MAP END":
                saw_end = True
                break
        if not saw_end:
            raise SystemExit("fixture did not finish the mapping capture")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="/dev/ttyUSB0")
    parser.add_argument("--seconds", type=float, default=40.0)
    args = parser.parse_args()
    main(args.port, args.seconds)
