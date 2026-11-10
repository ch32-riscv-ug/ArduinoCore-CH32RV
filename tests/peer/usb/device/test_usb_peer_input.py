"""usb.peer-input: the DUT (tests/sketch_support/usb_input) as a device with the HID classes usb_contracts
leaves out - an NKRO keyboard, a mouse, a gamepad, consumer control, system control and a custom report,
merged into one HID interface - and two CDC ports; the RP2040 (device/peer_pico, TinyUSB's host) reads
them. Each report is checked byte for byte as the host received it (report ID first), each followed by its
release; the CDC ports are told apart by their echo, and each port's line coding and control lines reach
that port's onLineCoding() / onLineState() alone."""
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).parent))
from device_peer import Peer, command, dut_ready, settings, wait_enumerated


def dut_cmd(dut, line, reply, timeout=5):
    dut.write(line + "\n")
    return dut.expect(reply + rb"\r?\n", timeout=timeout)


def lines(dut):
    m = dut_cmd(dut, "lines", rb"LINES" + rb" (\d+)" * 12)
    values = [int(v) for v in m.groups()]
    return values[:6], values[6:]


def test_usb_peer_input(dut, peers):
    peer = Peer(peers)
    build_id = settings(__file__)["build_id"]
    dut_ready(dut, build_id)
    dev = wait_enumerated(peer, build_id)
    assert (0x03, 0x00) in set(dev["interfaces"])
    assert [itf for itf in dev["interfaces"] if itf == (0x02, 0x02)] == [(0x02, 0x02)] * 2

    # Each report as the host received it, ID first, then the release.
    def report(line, expect, release):
        assert dut_cmd(dut, line, rb"\w+ (\d)").group(1) == b"1"
        peer.expect(rb"HIDIN \d+ " + expect, timeout=5)
        peer.expect(rb"HIDIN \d+ " + release, timeout=5)

    report("mouse 5 -3 1 2", rb"02" + rb"0205fd01", rb"02" + rb"00000000")
    report("pad 7 -2 3 5", rb"03" + rb"07fe00000000" + rb"03" + rb"05000000", rb"03" + rb"000000000000" + rb"00" + rb"00000000")
    report("consumer 0xe9", rb"04" + rb"e900", rb"04" + rb"0000")
    report("system 0x82", rb"05" + rb"02", rb"05" + rb"00")
    # NKRO: A..J (usages 4..13) held at once - bits 4..7 of byte 0 and 0..5 of byte 1 of the bitmap.
    report("nkro", rb"01" + rb"00" + rb"f03f" + rb"00" * 26, rb"01" + rb"00" * 29)
    assert dut_cmd(dut, "custom deadbeef", rb"CUSTOM (\d)").group(1) == b"1"
    peer.expect(rb"HIDIN \d+ 07deadbeef", timeout=5)

    # Two CDC ports: port A echoes, port B adds one.
    assert command(peer, "cdcports", rb"CDCPORTS (\d)").group(1) == b"2"
    echoes = {i: command(peer, f"cdcn {i} 0102fe", rb"CDCN \d (\w*)").group(1) for i in (0, 1)}
    assert sorted(echoes.values()) == [b"0102fe", b"0203ff"]
    a, b = (0, 1) if echoes[0] == b"0102fe" else (1, 0)
    # Across the packet boundary: 63, 64, 65 and 128 bytes, each back whole on both ports.
    for size in (63, 64, 65, 128):
        data = bytes((i * 3 + size) & 0xFF for i in range(size))
        assert command(peer, f"cdcn {a} {data.hex()}", rb"CDCN \d (\w*)").group(1).decode() == data.hex(), size
        back = bytes((x + 1) & 0xFF for x in data).hex()
        assert command(peer, f"cdcn {b} {data.hex()}", rb"CDCN \d (\w*)").group(1).decode() == back, size
    # Line coding and control lines reach each port's own callbacks.
    before = [int(v) for v in dut_cmd(dut, "events", rb"EVENTS (\d+) (\d+) (\d+) (\d+)").groups()]
    assert command(peer, f"cdcline {b} 9600 7 1 2", rb"CDCLINE (\d)").group(1) == b"1"
    assert command(peer, f"cdcctl {b} 1 0", rb"CDCCTL (\d)").group(1) == b"1"
    line_a, line_b = lines(dut)
    assert line_b == [9600, 7, 1, 2, 1, 0]
    assert command(peer, f"cdcline {a} 230400 8 0 0", rb"CDCLINE (\d)").group(1) == b"1"
    assert command(peer, f"cdcctl {a} 0 1", rb"CDCCTL (\d)").group(1) == b"1"
    line_a, line_b = lines(dut)
    assert line_a == [230400, 8, 0, 0, 0, 1] and line_b == [9600, 7, 1, 2, 1, 0]
    after = [int(v) for v in dut_cmd(dut, "events", rb"EVENTS (\d+) (\d+) (\d+) (\d+)").groups()]
    # Each port's callbacks ran (a host may send the control lines again with a line coding: EspUsbHost does,
    # TinyUSB does not); that nothing reached the other port is what the values above show.
    assert all(x - y >= 1 for x, y in zip(after, before)), (before, after)
    # A port writes only while the host holds DTR (connected()): port A, DTR off, sends nothing back; port B
    # still answers, and A again once DTR is back.
    assert command(peer, f"cdcn {a} 41", rb"CDCN \d (\w*)").group(1) == b""
    assert command(peer, f"cdcn {b} 41", rb"CDCN \d (\w*)").group(1) == b"42"
    assert command(peer, f"cdcctl {a} 1 0", rb"CDCCTL (\d)").group(1) == b"1"
    assert command(peer, f"cdcn {a} 43", rb"CDCN \d (\w*)").group(1) == b"43"
