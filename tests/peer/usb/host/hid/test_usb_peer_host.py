"""usb.peer-host: the DUT (usb_peer_host.ino, CH32RVUsbHost) as the USB host,
the ESP32-S3 (EspUsbDevice: keyboard, mouse, consumer control and vendor HID,
CDC, a FAT RAM disk) as the device.

Needs a DUT whose silicon has the USB host: CH32X035R8T6 / C8T6 / G8U6 / G8R6
(the F8U6, F7P6, D8U6 and the X033 are device-only)."""
import pathlib
import secrets
import sys

sys.path.insert(0, str(pathlib.Path(__file__).parent))
from host_peer import Peer, device, dut_ready, host, settings, wait_device


def test_usb_peer_host(dut, peers):
    peer = Peer(peers)
    dut_ready(dut, settings(__file__)["build_id"])
    device(peer, "?", rb"READY [01]")

    # Enumeration, strings, the interfaces the host drivers took.
    dev = wait_device(dut, peer.id["hid"])
    assert dev["vid"] == peer.id["vid"] and dev["speed"] == 1
    assert dev["manufacturer"] == peer.id["manufacturer"] and dev["serial"] == peer.id["hid_serial"]
    assert {(0x03, 0x00), (0x02, 0x02), (0x0A, 0x00), (0x08, 0x06)} <= set(dev["interfaces"])
    device(peer, "?", rb"READY 1")

    # CDC: binary bytes echoed, then 2000 bytes both ways at once, and the
    # device's own data.
    payload = bytes([0, 255, 85, 170]) + secrets.token_bytes(28)
    assert host(dut, "cdc " + payload.hex(), rb"CDC (\w*)").group(1).decode() == payload.hex()
    assert host(dut, "cdcbig 2000", rb"CDCBIG 2000 (\d) (\d+)", timeout=20).group(1) == b"1"
    assert host(dut, "baud 9600", rb"BAUD (\d)").group(1) == b"1"

    # Keyboard, mouse, consumer control from the device.
    device(peer, "type ab", rb"TYPED 1")
    for kind, code, ascii_ in ((b"p", 4, 97), (b"r", 4, 97), (b"p", 5, 98), (b"r", 5, 98)):
        dut.expect(b"H KEY " + kind + b" %d %d" % (code, ascii_), timeout=5)
    device(peer, "mouse 5 -3 1", rb"MOUSE 1")
    dut.expect(rb"H MOUSE 5 -3 0 1", timeout=5)
    device(peer, "consumer 0xe9", rb"CONSUMER 1")
    dut.expect(rb"H CONSUMER 233 p", timeout=5)
    dut.expect(rb"H CONSUMER 233 r", timeout=5)

    # Keyboard LEDs out, vendor HID out and back in incremented.
    assert host(dut, "leds 3", rb"LEDS (\d)").group(1) == b"1"
    assert device(peer, "leds", rb"LEDS (\d+)").group(1) == b"3"
    assert host(dut, "hidv 41" + b"vendor".hex(), rb"HIDV_SENT (\d)").group(1) == b"1"
    dut.expect(rb"H HIDVIN 42" + b"vendor".hex().encode(), timeout=5)

    # Mass storage: inquiry, capacity, a raw block written, read back, restored.
    m = host(dut, "msc", rb"MSC (\d) (\S+) (.+) (\d+) (\d+)", timeout=20)
    assert m.group(1) == b"1" and int(m.group(5)) == 512 and int(m.group(4)) >= 64
    assert host(dut, "mscraw 100", rb"MSCRAW (\d)", timeout=20).group(1) == b"1"
