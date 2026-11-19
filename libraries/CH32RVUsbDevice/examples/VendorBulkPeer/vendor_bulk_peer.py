#!/usr/bin/env python3
"""The PC side of VendorBulkPeer (PyUSB): two phases against the device's back-pressured echo.

1. Steady: chunks of uneven sizes (1 to 300 bytes) written while reading, every byte checked in order.
2. Stalled sender: 8 KB written without reading. The device stops reading once its IN side is full, so a write
   times out part-way and says how much it took; then the PC reads everything back and finishes the rest -
   nothing lost, nothing reordered.

    pip install pyusb      (and libusb; on Windows bind WinUSB, on Linux give access to the device)
    python3 vendor_bulk_peer.py
"""
import random
import sys

import usb.core
import usb.util

dev = usb.core.find(idVendor=0x1209, idProduct=0x0001, product="CH32RV VendorBulkPeer")
if dev is None:
    sys.exit("VendorBulkPeer not found")
cfg = dev.get_active_configuration()
itf = next(i for i in cfg if i.bInterfaceClass == 0xFF)
ep_in = next(e for e in itf if e.bEndpointAddress & 0x80)
ep_out = next(e for e in itf if not e.bEndpointAddress & 0x80)


def read_some(want, timeout=200):
    got = bytearray()
    while len(got) < want:
        try:
            got += bytes(ep_in.read(4096, timeout=timeout))
        except usb.core.USBTimeoutError:
            break
    return bytes(got)


# 1. steady
rng = random.Random(1)
sent = bytearray()
echoed = bytearray()
for _ in range(200):
    chunk = bytes(rng.randrange(256) for _ in range(rng.randint(1, 300)))
    ep_out.write(chunk, timeout=1000)
    sent += chunk
    echoed += read_some(len(sent) - len(echoed), timeout=50)
echoed += read_some(len(sent) - len(echoed), timeout=500)
print(f"steady: {len(sent)} bytes sent, {len(echoed)} back, {'same' if echoed == sent else 'DIFFERENT'}")

# 2. stalled sender
block = bytes(i & 0xFF for i in range(8192))
taken = 0
try:
    taken = ep_out.write(block, timeout=500)
except usb.core.USBTimeoutError as e:
    # PyUSB raises with what went through unknown on some backends; ask the device's echo instead
    print(f"stalled: the write timed out ({e})")
back = read_some(len(block), timeout=500)
print(f"stalled: the write took {taken or len(back)} bytes before the device stopped reading, {len(back)} came back")
rest = block[len(back):]
if rest:
    ep_out.write(rest, timeout=2000)
    back += read_some(len(rest), timeout=500)
print(f"after resuming: {len(back)} of {len(block)} back, {'in order, none lost' if back == block else 'DIFFERENT'}")
