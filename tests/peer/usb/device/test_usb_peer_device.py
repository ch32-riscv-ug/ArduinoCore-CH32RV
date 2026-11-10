"""usb.peer-device: the DUT (tests/sketch_support/usb_contracts - CDC, keyboard
and vendor HID, MIDI, NCM, DFU runtime) as a device, an ESP32-S3 with
EspUsbHost as the host.

The same contracts as instrumented/usb/test_usb_contract.py (where the PC is
the host), seen from an independent host stack. NCM is in test_usb_peer_net.py:
the S3 has eight host channels and runs out once CDC, HID and MIDI have theirs.
"""
import pathlib
import secrets
import sys

sys.path.insert(0, str(pathlib.Path(__file__).parent))
from device_peer import Peer, DEVICE_VID_PID, ISP_ID_PATTERN, command, dut_ready, settings, wait_enumerated


def test_usb_peer_device(dut, peers, record_property):
    peer = Peer(peers)
    build_id = settings(__file__)["build_id"]
    dut_ready(dut, build_id)
    command(peer, "ping", rb"PONG")

    # Enumeration: this build, at the link's speed (1 full, 2 high: the ESP32-P4 is on the DUT's USBHS), every
    # declared function.
    dev = wait_enumerated(peer, build_id)
    assert (dev["vid"], dev["pid"]) == DEVICE_VID_PID
    speed = 2 if settings(__file__)["target"].get("usb_peer") == "p4" else 1
    assert dev["speed"] == speed and dev["usb"] == "0210"
    if peer.kind == "s3":
        # The descriptors are this speed's: bulk endpoints 512 bytes at high speed, 64 at full speed (PR #1: the
        # USBHS bus reset can come before the high-speed handshake ends, and the speed read then is full).
        peer.write("eps\n")
        block = peer.serial.expect(rb"((?:S3 EP \d+ \w+ \w+ \d+\r?\n)*)S3 EPEND (\d+)", timeout=5)
        eps = [line.split()[2:] for line in block.group(1).decode().splitlines() if line.strip()]
        bulk = {int(mps) for itf, address, attributes, mps in eps if int(attributes, 16) & 3 == 2}
        assert bulk == {512 if speed == 2 else 64}, eps
    assert {(0x02, 0x02), (0x02, 0x0D), (0x0A, 0x00), (0x03, 0x00), (0x01, 0x03), (0xFE, 0x01)} <= set(dev["interfaces"])

    # CDC: binary bytes echoed, then 2000 bytes in both directions at once.
    payload = bytes([0, 255, 85, 170]) + secrets.token_bytes(28)
    assert command(peer, "cdc " + payload.hex(), rb"CDC (\w*)").group(1).decode() == payload.hex()
    assert command(peer, "cdcbig 2000", rb"CDCBIG 2000 (\d) (\d+)", timeout=20).group(1) == b"1"
    assert command(peer, "baud 9600", rb"BAUD (\d)").group(1) == b"1"
    # The same echo through onRxData() and writeDirect() (CH32RVUsbDeviceBulkStream,
    # shared with Vendor), then back to the FIFOs.
    dut.write("cdcmode direct\n")
    before = int(dut.expect(rb"CDCMODE direct (\d+)\r?\n", timeout=5).group(1))
    for _ in range(3):
        payload = secrets.token_bytes(32)
        assert command(peer, "cdc " + payload.hex(), rb"CDC (\w*)").group(1).decode() == payload.hex()
    dut.write("cdcmode fifo\n")
    assert int(dut.expect(rb"CDCMODE fifo (\d+)\r?\n", timeout=5).group(1)) - before == 3
    payload = secrets.token_bytes(32)
    assert command(peer, "cdc " + payload.hex(), rb"CDC (\w*)").group(1).decode() == payload.hex()

    # Vendor HID: report 6 out, back in with its first byte incremented.
    command(peer, "hidv 0641" + b"vendor".hex(), rb"HIDV_SENT 1")
    peer.expect(rb"HIDVIN 42" + b"vendor".hex().encode(), timeout=5)

    # Keyboard: the DUT types "ab".
    dut.write("keys\n")
    for kind, code, ascii_ in ((b"p", 4, 97), (b"r", 4, 97), (b"p", 5, 98), (b"r", 5, 98)):
        peer.expect(b"KEY " + kind + b" %d %d" % (code, ascii_), timeout=5)
    dut.expect_exact("KEYS 1", timeout=5)
    assert command(peer, "leds 3", rb"LEDS (\d)").group(1) == b"1"

    # SET_IDLE 100 ms (report ID 0: every report): the vendor HID's last input
    # report comes again about ten times a second; idle 0 stops it.
    m = command(peer, "hididle 25 1000", rb"HIDIDLE (\d) (\d+)", timeout=10)
    assert m.group(1) == b"1" and 7 <= int(m.group(2)) <= 12
    m = command(peer, "hididle 0 500", rb"HIDIDLE (\d) (\d+)", timeout=10)
    assert m.group(1) == b"1" and int(m.group(2)) <= 1

    # MIDI: note 64 comes back as note 76.
    command(peer, "midi 0 64 127", rb"MIDI_SENT 1")
    peer.expect(rb"MIDI 9 90 76 127", timeout=5)

    # Reconnect: end() + begin() is an unplug and a plug to the S3.
    dut.write("mounts\n")
    mounts = int(dut.expect(rb"MOUNTS (\d+)\r?\n", timeout=5).group(1))
    dut.write("replug\n")
    peer.expect(rb"DISCONNECTED \d+ 1209 0001", timeout=10)
    dut.expect_exact("REPLUG 1", timeout=10)
    peer.expect(rb"CONNECTED \d+ 1209 0001", timeout=15)
    dut.write("mounts\n")
    assert int(dut.expect(rb"MOUNTS (\d+)\r?\n", timeout=5).group(1)) > mounts

    # DFU_DETACH from the peer: on the X035 the factory bootloader appears on the same port, and without an
    # upload it hands back to this build; elsewhere enterBootloader() is a plain reset (CH32RVUsbDevicePort.h:
    # the other series reach their bootloader by BOOT0 alone), so this build comes straight back.
    wait_enumerated(peer, build_id)
    command(peer, "dfu", rb"DFU 1 \d")
    peer.expect(rb"DISCONNECTED \d+ 1209 0001", timeout=10)
    if settings(__file__)["target"]["family"].startswith("CH32X03"):
        peer.expect(rb"CONNECTED \d+ " + ISP_ID_PATTERN, timeout=10)
    peer.expect(rb"CONNECTED \d+ 1209 0001", timeout=40)
    wait_enumerated(peer, build_id)
