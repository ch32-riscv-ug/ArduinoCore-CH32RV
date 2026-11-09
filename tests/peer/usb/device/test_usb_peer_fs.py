"""usb.peer-fs: the FS library's FAT against ESP-IDF's (tests/sketch_support/usb_fs).
The DUT formats a volume with FatFS and writes files, then shows it over USB
mass storage; the ESP32-S3 (EspUsbHost, ESP-IDF's FAT) reads README.TXT and
writes HOST.TXT; the DUT takes it back, finds its own files intact (a long
directory and file name among them) and reads HOST.TXT; then it rewrites
README.TXT in place and the S3 reads the new line. Twice."""
import pathlib
import secrets
import sys

sys.path.insert(0, str(pathlib.Path(__file__).parent))
from device_peer import Peer, command, dut_ready, settings, wait_enumerated


def ready_again(peer):
    """The medium came back: the S3 gets UNIT ATTENTION once, then it is ready."""
    for _ in range(4):
        if command(peer, "msctur", rb"MSCTUR (\d) [^\r\n]*").group(1) == b"1":
            return
    raise AssertionError("the medium did not become ready for the S3")


def test_usb_peer_fs(dut, peers):
    peer = Peer(peers)
    build_id = settings(__file__)["build_id"]
    dut_ready(dut, build_id)
    wait_enumerated(peer, build_id)

    dut.write("prep\n")
    m = dut.expect(rb"PREP (\d) (\d+)\r?\n", timeout=15)
    assert m.group(1) == b"1", "FatFS could not format and write the volume"
    ready_again(peer)
    m = command(peer, "msc", rb"MSC (\d) (\S+) (.+) (\d+) (\d+)", timeout=20)
    assert m.group(1) == b"1" and int(m.group(4)) == 128 and int(m.group(5)) == 512

    expected = f"CH32RV usb-fs {build_id}"
    for round_ in range(2):
        m = command(peer, "mscread", rb"MSCREAD (\d) ([^\r\n]*)", timeout=20)
        assert m.group(1) == b"1", "ESP-IDF's FAT could not mount what FatFS wrote"
        assert m.group(2).decode() == expected
        text = f"S3 round {round_} {secrets.token_hex(4)}"
        assert command(peer, "mscwrite " + text, rb"MSCWRITE (\d)", timeout=20).group(1) == b"1"

        token = secrets.token_hex(4)
        dut.write(f"take {token}\n")
        m = dut.expect(rb"TAKE (\d) (\d) (\d+) ([^\r\n]*)\r?\n", timeout=15)
        assert m.group(1) == b"1", "FatFS could not mount what ESP-IDF's FAT left"
        assert m.group(2) == b"1", "the DUT's own files did not survive the S3's write"
        assert int(m.group(3)) == 4          # README.TXT, CFG.TXT, the directory, HOST.TXT
        assert m.group(4).decode() == text
        expected = f"CH32RV usb-fs {token}"
        ready_again(peer)

    m = command(peer, "mscread", rb"MSCREAD (\d) ([^\r\n]*)", timeout=20)
    assert m.group(1) == b"1" and m.group(2).decode() == expected
    dut.write("slots\n")
    m = dut.expect(rb"SLOTS (\d+) (\d+)\r?\n", timeout=5)
    assert m.group(2) == b"0", "the sparse medium ran out of slots"
