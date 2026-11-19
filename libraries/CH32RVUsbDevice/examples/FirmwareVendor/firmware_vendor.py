#!/usr/bin/env python3
"""Send a sketch's image to the FirmwareVendor sketch over its vendor interface.

    uv run --with pyusb python3 firmware_vendor.py Blink.ino.bin

START (the size), the image on bulk OUT in 16 KiB transfers, STATUS until the device has written it all, then COMMIT:
the device checks the image (a stall when it is not a sketch for it) and restarts into it. On Linux the device node
needs write access (a udev rule for 1209:0001, or sudo).
"""
import sys
import time

import usb.core
import usb.util

OUT, IN = 0x40, 0xC0                     # vendor, to the device
START, COMMIT, ABORT, STATUS = 1, 2, 3, 4


def status(dev):
    data = bytes(dev.ctrl_transfer(IN, STATUS, 0, 0, 8, timeout=5000))
    return data[0], int.from_bytes(data[1:4], "little"), int.from_bytes(data[4:8], "little")


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    image = open(sys.argv[1], "rb").read()
    dev = usb.core.find(idVendor=0x1209, idProduct=0x0001, product="CH32RV FirmwareVendor")
    if dev is None:
        sys.exit("no CH32RV FirmwareVendor device (1209:0001)")
    itf = next(i for i in dev.get_active_configuration() if i.bInterfaceClass == 0xFF)
    out = next(e for e in itf if not e.bEndpointAddress & 0x80)
    usb.util.claim_interface(dev, itf.bInterfaceNumber)
    size = len(image)
    print(f"device  {dev.product}, bulk OUT 0x{out.bEndpointAddress:02x} ({out.wMaxPacketSize} bytes)")
    dev.ctrl_transfer(OUT, START, size & 0xFFFF, size >> 16, None, timeout=5000)
    active, error, _ = status(dev)
    if not active:
        sys.exit(f"device refused the image ({size} bytes): Update error {error}")
    started = time.monotonic()
    for offset in range(0, size, 16384):
        out.write(image[offset:offset + 16384], timeout=10000)
        print(f"\r{min(offset + 16384, size)}/{size} bytes", end="", flush=True)
    print()
    deadline = time.monotonic() + 30
    while True:
        active, error, written = status(dev)
        if not active:
            sys.exit(f"device stopped at {written} bytes: Update error {error}")
        if written >= size:
            break
        if time.monotonic() > deadline:
            dev.ctrl_transfer(OUT, ABORT, 0, 0, None, timeout=5000)
            sys.exit(f"device wrote {written} of {size} bytes in 30 s: aborted")
        time.sleep(0.05)
    print(f"written in {time.monotonic() - started:.2f} s")
    try:
        dev.ctrl_transfer(OUT, COMMIT, 0, 0, None, timeout=10000)
    except usb.core.USBError as e:
        _, error, _ = status(dev)
        sys.exit(f"device refused to install it ({e}): Update error {error}")
    print("accepted: the device restarts into the new sketch")


if __name__ == "__main__":
    main()
