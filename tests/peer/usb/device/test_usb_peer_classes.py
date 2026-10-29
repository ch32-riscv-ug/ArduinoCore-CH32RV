"""usb.peer-classes: the DUT (tests/sketch_support/usb_classes - vendor bulk
with a vendor request, a FAT RAM disk, a smart card reader, DFU runtime) as a
device, the ESP32-S3 (EspUsbHost) as the host."""
import pathlib
import secrets
import sys

sys.path.insert(0, str(pathlib.Path(__file__).parent))
from s3_peer import command, dut_ready, settings, wait_enumerated


def test_usb_peer_classes(dut, peers):
    s3 = peers["s3"]
    build_id = settings(__file__)["build_id"]
    dut_ready(dut, build_id)
    dev = wait_enumerated(s3, build_id)
    # The vendor interface carries setInterfaceProtocol()'s subclass (0x4F).
    assert {(0xFF, 0x4F), (0x08, 0x06), (0x0B, 0x00), (0xFE, 0x01)} <= set(dev["interfaces"])

    # Vendor bulk echoes; vendor request 0x10 answers "CH32".
    data = secrets.token_bytes(48)
    assert command(s3, "vend " + data.hex(), rb"VEND (\w*)").group(1).decode() == data.hex()
    # onBusEvent() saw the configuration, onAnyControlRequest() the enumeration's
    # transfers; the descriptor getters give the device's own VID:PID and a
    # configuration of the length the S3 read (no HID here: 0).
    dut.write("events\n")
    m = dut.expect(rb"EVENTS (\d+) (\d+) (\d+) (\d+) (\d+) (\w{4}):(\w{4}) (\d+)\r?\n", timeout=5)
    assert int(m.group(1)) >= 1 and int(m.group(2)) >= 1
    assert int(m.group(4)) >= 5 and int(m.group(3)) >= int(m.group(4))
    assert int(m.group(5)) > 9 * 5
    assert (m.group(6).decode(), m.group(7).decode()) == (dev["vid"], dev["pid"])
    assert int(m.group(8)) == 0
    # The same echo through onRxData() and writeDirect(), completions counted
    # by a capturing lambda; then back to the class FIFOs.
    dut.write("vmode direct\n")
    before = int(dut.expect(rb"VMODE direct (\d+) \d+ \d+\r?\n", timeout=5).group(1))
    for _ in range(3):
        data = secrets.token_bytes(48)
        assert command(s3, "vend " + data.hex(), rb"VEND (\w*)").group(1).decode() == data.hex()
    dut.write("vmode fifo\n")
    m = dut.expect(rb"VMODE fifo (\d+) (\d+) (\d+)\r?\n", timeout=5)
    after = int(m.group(1))
    assert after - before == 3
    # The FIFO echoes before peeked a byte ahead each time, and it was the one read() returned.
    assert int(m.group(2)) >= 1 and int(m.group(3)) == 0
    data = secrets.token_bytes(48)
    assert command(s3, "vend " + data.hex(), rb"VEND (\w*)").group(1).decode() == data.hex()
    m = command(s3, "vreq", rb"VREQ (\d) (\S*)")
    assert m.group(1) == b"1" and m.group(2) == b"CH32"

    # Mass storage: inquiry and capacity, a raw block written, read back and
    # restored, the volume's README read and a file written for the DUT.
    m = command(s3, "msc", rb"MSC (\d) (\S+) (.+) (\d+) (\d+)", timeout=20)
    assert m.group(1) == b"1" and int(m.group(5)) == 512 and int(m.group(4)) >= 16
    assert command(s3, "mscraw 19", rb"MSCRAW (\d)", timeout=20).group(1) == b"1"
    m = command(s3, "mscread", rb"MSCREAD (\d) ([^\r\n]*)", timeout=20)
    assert m.group(1) == b"1" and build_id.encode() in m.group(2)
    text = "from the S3 " + secrets.token_hex(4)
    assert command(s3, "mscwrite " + text, rb"MSCWRITE (\d)", timeout=20).group(1) == b"1"
    dut.write("file\n")
    m = dut.expect(rb"FILE (\d+) ([^\r\n]*)\r?\n", timeout=5)
    assert m.group(2).decode() == text

    # Smart card: power on returns the DUT's ATR, an APDU comes back reversed + 90 00.
    m = command(s3, "ccid", rb"CCID (\d) (\w*)", timeout=15)
    assert m.group(1) == b"1" and bytes.fromhex(m.group(2).decode())[:2] == b"\x3b\x8a"
    apdu = bytes([0x00, 0xA4, 0x04, 0x00, 0x03, 0x01, 0x02, 0x03])
    m = command(s3, "apdu " + apdu.hex(), rb"APDU (\d) (\w*) (\w{4})", timeout=15)
    assert m.group(1) == b"1" and m.group(3) == b"9000"
    assert bytes.fromhex(m.group(2).decode()) == apdu[::-1]     # EspUsbHost returns SW apart
    dut.write("card\n")
    m = dut.expect(rb"CARD (\d) (\d) (\d+)\r?\n", timeout=5)
    assert m.group(1) == b"1" and m.group(2) == b"1" and int(m.group(3)) >= 1

    # Borrowed receive (last: it restarts the device). The echo lands in
    # readDirect()'s buffer and goes back through writeDirect(); a posted
    # receive is cancelled and handed back before cancelDirectRx() returns.
    dut.write("vborrow\n")
    m = dut.expect(rb"VBORROW (\d) (\d)\r?\n", timeout=5)
    assert m.group(1) == b"1" and m.group(2) == b"1"
    wait_enumerated(s3, build_id)
    for _ in range(3):
        data = secrets.token_bytes(48)
        assert command(s3, "vend " + data.hex(), rb"VEND (\w*)").group(1).decode() == data.hex()
    dut.write("vcancel\n")
    m = dut.expect(rb"VCANCEL (\d) (\d) (\d) (\d+) (\d+)\r?\n", timeout=5)
    assert m.group(1, 2, 3) == (b"1", b"0", b"0")
    assert int(m.group(4)) == 1 and int(m.group(5)) == 3
    data = secrets.token_bytes(48)
    assert command(s3, "vend " + data.hex(), rb"VEND (\w*)").group(1).decode() == data.hex()
