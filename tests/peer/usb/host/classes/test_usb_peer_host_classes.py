"""usb.peer-host-classes: the DUT (usb_peer_host.ino, CH32RVUsbHost) as the
USB host, the ESP32-S3 (EspUsbDevice: MIDI, vendor bulk with a vendor
request, a smart card reader) as the device. Same silicon requirement as
usb.peer-host."""
import pathlib
import secrets
import sys

sys.path.insert(0, str(pathlib.Path(__file__).parent))
from host_peer import Peer, device, dut_ready, host, settings, wait_device


def test_usb_peer_host_classes(dut, peers):
    peer = Peer(peers)
    dut_ready(dut, settings(__file__)["build_id"])
    dev = wait_device(dut, peer.id["classes"])
    assert dev["vid"] == peer.id["vid"]
    assert {(0x01, 0x03), (0xFF, 0x00), (0x0B, 0x00)} <= set(dev["interfaces"])
    device(peer, "?", rb"READY 1")

    # MIDI: note 64 comes back as 76, and the device's own note arrives.
    assert host(dut, "midi 0 64 100", rb"MIDI_SENT (\d)").group(1) == b"1"
    dut.expect(rb"H MIDI 9 90 76 100", timeout=5)
    device(peer, "midisend 2 60 33", rb"MIDISENT 1")
    dut.expect(rb"H MIDI 9 92 60 33", timeout=5)

    # Vendor bulk echoes; vendor request 0x10 answers "ESP32S3".
    data = secrets.token_bytes(48)
    assert host(dut, "vend " + data.hex(), rb"VEND (\w*)").group(1).decode() == data.hex()
    m = host(dut, "vreq", rb"VREQ (\d) (\S*)")
    assert m.group(1) == b"1" and m.group(2) == peer.id["vreq"].encode()

    # Smart card: the ATR, an APDU reversed + 90 00.
    m = host(dut, "ccid", rb"CCID (\d) (\w*)", timeout=15)
    assert m.group(1) == b"1" and bytes.fromhex(m.group(2).decode())[:2] == b"\x3b\x88"
    apdu = bytes([0x00, 0xA4, 0x04, 0x00, 0x02, 0x3F, 0x00])
    m = host(dut, "apdu " + apdu.hex(), rb"APDU (\d) (\w*) (\w{4})", timeout=15)
    assert m.group(1) == b"1" and m.group(3) == b"9000"
    assert bytes.fromhex(m.group(2).decode()) == apdu[::-1]
    assert int(device(peer, "card", rb"CARD (\d+)").group(1)) >= 1
