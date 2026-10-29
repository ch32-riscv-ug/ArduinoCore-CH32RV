"""usb.peer-net: the DUT (tests/sketch_support/usb_net - CDC-NCM alone) as a
device, the ESP32-S3 (EspUsbHost) as the host: Ethernet frames both ways."""
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).parent))
from s3_peer import command, dut_ready, settings, wait_enumerated


def test_usb_peer_net(dut, peers):
    s3 = peers["s3"]
    build_id = settings(__file__)["build_id"]
    dut_ready(dut, build_id)
    dev = wait_enumerated(s3, build_id)
    assert {(0x02, 0x0D), (0x0A, 0x00)} <= set(dev["interfaces"])

    assert command(s3, "net", rb"NETOPEN (\d)", timeout=10).group(1) == b"1"
    command(s3, "reset", rb"RESET")
    dut.write("net\n")
    before = dut.expect(rb"NET (\d+) (\d+) ([0-9A-F]{12})\r?\n", timeout=5)

    # S3 -> DUT, and the DUT's echo back.
    assert command(s3, "netsend", rb"NETSENT (\d)").group(1) == b"1"
    dut.write("net\n")
    after = dut.expect(rb"NET (\d+) (\d+) ([0-9A-F]{12})\r?\n", timeout=5)
    assert int(after.group(2)) == int(before.group(2)) + 1

    # DUT -> S3.
    dut.write("netsend\n")
    dut.expect_exact("NETSEND 1", timeout=5)
    m = command(s3, "netstat", rb"NETRX (\d+) (\d+)")
    assert int(m.group(2)) >= 2       # the echo and the DUT's own frame

    # 20 frames back to back: with two transmit blocks, those sent while one
    # is out go together in the next; every one reaches the S3.
    before = int(command(s3, "netstat", rb"NETRX (\d+) (\d+)").group(2))
    dut.write("netburst 20\n")
    m = dut.expect(rb"NETBURST (\d+) (\d+) (\d+) (\d+) (\d+)\r?\n", timeout=15)
    assert int(m.group(1)) == 20
    assert int(m.group(3)) == 2048 and int(m.group(4)) == 2 and int(m.group(5)) == 2
    sent = int(m.group(2))
    assert sent >= 20
    m = command(s3, "netstat", rb"NETRX (\d+) (\d+)")
    assert int(m.group(2)) - before == 20
