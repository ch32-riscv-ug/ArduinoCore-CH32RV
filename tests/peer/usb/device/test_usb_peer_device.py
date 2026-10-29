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
from s3_peer import DEVICE_VID_PID, ISP_ID_PATTERN, command, dut_ready, settings, wait_enumerated


def test_usb_peer_device(dut, peers, record_property):
    s3 = peers["s3"]
    build_id = settings(__file__)["build_id"]
    dut_ready(dut, build_id)
    command(s3, "ping", rb"PONG")

    # Enumeration: this build, full speed, every declared function.
    dev = wait_enumerated(s3, build_id)
    assert (dev["vid"], dev["pid"]) == DEVICE_VID_PID
    assert dev["speed"] == 1 and dev["usb"] == "0210"
    assert {(0x02, 0x02), (0x02, 0x0D), (0x0A, 0x00), (0x03, 0x00), (0x01, 0x03), (0xFE, 0x01)} <= set(dev["interfaces"])

    # CDC: binary bytes echoed, then 2000 bytes in both directions at once.
    payload = bytes([0, 255, 85, 170]) + secrets.token_bytes(28)
    assert command(s3, "cdc " + payload.hex(), rb"CDC (\w*)").group(1).decode() == payload.hex()
    assert command(s3, "cdcbig 2000", rb"CDCBIG 2000 (\d) (\d+)", timeout=20).group(1) == b"1"
    assert command(s3, "baud 9600", rb"BAUD (\d)").group(1) == b"1"

    # Vendor HID: report 6 out, back in with its first byte incremented.
    command(s3, "hidv 0641" + b"vendor".hex(), rb"HIDV_SENT 1")
    s3.expect(rb"S3 HIDVIN 42" + b"vendor".hex().encode(), timeout=5)

    # Keyboard: the DUT types "ab".
    dut.write("keys\n")
    for kind, code, ascii_ in ((b"p", 4, 97), (b"r", 4, 97), (b"p", 5, 98), (b"r", 5, 98)):
        s3.expect(b"S3 KEY " + kind + b" %d %d" % (code, ascii_), timeout=5)
    dut.expect_exact("KEYS 1", timeout=5)
    assert command(s3, "leds 3", rb"LEDS (\d)").group(1) == b"1"

    # SET_IDLE 100 ms (report ID 0: every report): the vendor HID's last input
    # report comes again about ten times a second; idle 0 stops it.
    m = command(s3, "hididle 25 1000", rb"HIDIDLE (\d) (\d+)", timeout=10)
    assert m.group(1) == b"1" and 7 <= int(m.group(2)) <= 12
    m = command(s3, "hididle 0 500", rb"HIDIDLE (\d) (\d+)", timeout=10)
    assert m.group(1) == b"1" and int(m.group(2)) <= 1

    # MIDI: note 64 comes back as note 76.
    command(s3, "midi 0 64 127", rb"MIDI_SENT 1")
    s3.expect(rb"S3 MIDI 9 90 76 127", timeout=5)

    # Reconnect: end() + begin() is an unplug and a plug to the S3.
    dut.write("mounts\n")
    mounts = int(dut.expect(rb"MOUNTS (\d+)\r?\n", timeout=5).group(1))
    dut.write("replug\n")
    s3.expect(rb"S3 DISCONNECTED \d+ 1209 0001", timeout=10)
    dut.expect_exact("REPLUG 1", timeout=10)
    s3.expect(rb"S3 CONNECTED \d+ 1209 0001", timeout=15)
    dut.write("mounts\n")
    assert int(dut.expect(rb"MOUNTS (\d+)\r?\n", timeout=5).group(1)) > mounts

    # DFU_DETACH from the S3: the factory bootloader appears on the same port,
    # and without an upload it hands back to this build.
    wait_enumerated(s3, build_id)
    command(s3, "dfu", rb"DFU 1")
    s3.expect(rb"S3 DISCONNECTED \d+ 1209 0001", timeout=10)
    s3.expect(rb"S3 CONNECTED \d+ " + ISP_ID_PATTERN, timeout=10)
    s3.expect(rb"S3 CONNECTED \d+ 1209 0001", timeout=40)
    wait_enumerated(s3, build_id)
