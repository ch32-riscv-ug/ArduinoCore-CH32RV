#!/usr/bin/env python3
"""Send a sketch image (.bin) to the FirmwareCDC sketch over its USB serial port.

    python3 firmware_cdc.py /dev/ttyACM0 build/Blink.ino.bin

Needs pyserial. Without installing anything:

    uv run --with pyserial python3 firmware_cdc.py /dev/ttyACM0 sketch.bin
"""
import sys
import time

import serial


def read_line(port, timeout=10.0):
    deadline = time.time() + timeout
    line = b""
    while time.time() < deadline:
        byte = port.read(1)
        if not byte:
            continue
        if byte in b"\r\n":
            if line:
                return line.decode("utf-8", "replace")
            continue
        line += byte
    raise TimeoutError("no reply from the device")


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    path, image_path = sys.argv[1], sys.argv[2]
    image = open(image_path, "rb").read()
    port = serial.Serial(path, 115200, timeout=0.2)
    # An image the device refused part way leaves its rest read as command lines: a newline ends the last of them, and
    # their ERR replies are read until the port has been quiet for 0.3 s.
    port.write(b"\n")
    port.flush()
    port.reset_input_buffer()
    quiet = time.time() + 0.3
    while time.time() < quiet:
        if port.read(256):
            quiet = time.time() + 0.3
    port.write(f"FW {len(image)}\n".encode())
    port.flush()
    reply = read_line(port)
    if reply != "READY":
        sys.exit(f"device refused: {reply}")
    started = time.time()
    sent = 0
    for offset in range(0, len(image), 1024):
        chunk = image[offset:offset + 1024]
        port.write(chunk)
        sent += len(chunk)
        print(f"\r{sent}/{len(image)} bytes", end="", flush=True)
    port.flush()
    print()
    reply = read_line(port, timeout=30.0)
    elapsed = time.time() - started
    if reply != "OK":
        sys.exit(f"device refused the image: {reply}")
    print(f"accepted {len(image)} bytes in {elapsed:.1f}s; the board is restarting into it")


if __name__ == "__main__":
    main()
