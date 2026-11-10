"""usb.peer-host-input: the DUT (usb_peer_host.ino, CH32RVUsbHost) as the USB host, the RP2040
(host_input/peer_pico) as the device: a gamepad, system control and a vendor collection with input,
output and feature reports on one HID interface, a boot-protocol keyboard on another, and two CDC ports.

What usb-peer-host leaves out: the gamepad and system control events, raw HID input, the report
descriptors, GET_REPORT / SET_REPORT of any type, the vendor feature report, SET_PROTOCOL, the keyboard's
lock state and layout, a second CDC port (onSerialData(), the line coding and control lines it sets), and
the device going away and coming back. Same silicon requirement as usb-peer-host."""
import pathlib
import sys
import time

sys.path.insert(0, str(pathlib.Path(__file__).parent))
from host_peer import Peer, device, dut_ready, host, settings, wait_device


def serial_rx(dut, port, want, timeout=3):
    """What arrived on a CDC port, polled until `want` bytes are there (hex)."""
    got = ""
    deadline = time.monotonic() + timeout
    while len(got) < 2 * want and time.monotonic() < deadline:
        got += host(dut, f"srx {port}", rb"SRX \d (\w*)").group(1).decode()
        time.sleep(0.1)
    return got


def test_usb_peer_host_input(dut, peers):
    peer = Peer(peers)
    dut_ready(dut, settings(__file__)["build_id"])
    dev = wait_device(dut, peer.id["input"])
    assert dev["vid"] == peer.id["vid"]
    device(peer, "?", rb"READY 1")
    hids = {proto: number for number, alt, cls, sub, proto in dev["itfs"] if cls == 0x03 and alt == 0}
    hid, keyboard = hids[0], hids[1]

    # The report descriptors the host read are the device's, byte for byte in length.
    m = device(peer, "desclen", rb"DESCLEN (\d+) (\d+)")
    dut.write("hiddesc\n")
    block = dut.expect(rb"((?:H HIDDESC \d+ \d+\r?\n)*)H HIDDESCEND", timeout=5).group(1).decode()
    lengths = {int(a): int(b) for a, b in (line.split()[2:] for line in block.split("\n") if line.strip())}
    assert lengths[hid] == int(m.group(1)) and lengths[keyboard] == int(m.group(2))

    # Gamepad (x, y, z, rz, rx, ry, hat, 32 buttons), then all zero.
    device(peer, "gamepad 5 -3 9", rb"GAMEPAD 1")
    dut.expect(rb"H GAMEPAD 05fd000000000009000000", timeout=5)
    dut.expect(rb"H GAMEPAD 0000000000000000000000", timeout=5)
    # System control: selector 2 is Usage 0x82, System Sleep.
    device(peer, "system 2", rb"SYSTEM 1")
    dut.expect(rb"H SYSTEM 130 p", timeout=5)
    dut.expect(rb"H SYSTEM 130 r", timeout=5)

    # Vendor reports, ID 3: input; output through the interrupt OUT and through SET_REPORT; feature both ways.
    device(peer, "vin 0a0b0c", rb"VIN 1")
    dut.expect(rb"H HIDVIN 0a0b0c0000000000", timeout=5)
    assert host(dut, "hidv 1122", rb"HIDV_SENT (\d)").group(1) == b"1"
    assert host(dut, "hidset %d 2 3 33445566" % hid, rb"HIDSET (\d)").group(1) == b"1"
    assert host(dut, "hidfeat a1b2c3", rb"HIDFEAT (\d)").group(1) == b"1"
    m = device(peer, "last", rb"LAST (\w+) (\w+)")
    assert m.group(1).startswith(b"33445566") and m.group(2).startswith(b"a1b2c3")
    device(peer, "feature 0102030405060708", rb"FEATURE")
    m = host(dut, "hidget %d 3 3 9" % hid, rb"HIDGET (\d) (\w*)")
    assert m.group(1) == b"1" and m.group(2).endswith(b"0102030405060708")
    # onHIDInput(): the report as it came, ID first.
    host(dut, "hidraw 1", rb"HIDRAW 1")
    device(peer, "vin 0d", rb"VIN 1")
    dut.expect(rb"H HIDIN %d 030d" % hid, timeout=5)
    host(dut, "hidraw 0", rb"HIDRAW 0")

    # SET_PROTOCOL on the keyboard: boot, then report again.
    assert device(peer, "proto", rb"PROTO (\d)").group(1) == b"1"
    assert host(dut, "proto %d 1" % keyboard, rb"PROTO (\d)").group(1) == b"1"
    assert device(peer, "proto", rb"PROTO (\d)").group(1) == b"0"
    assert host(dut, "proto %d 0" % keyboard, rb"PROTO (\d)").group(1) == b"1"
    assert device(peer, "proto", rb"PROTO (\d)").group(1) == b"1"

    # Caps Lock set by the host: the lock state, the LED report, and 'a' arriving as 'A'.
    assert host(dut, "leds 2", rb"LEDS (\d)").group(1) == b"1"
    assert device(peer, "leds", rb"LEDS (\d+)").group(1) == b"2"
    assert host(dut, "locks", rb"LOCKS (\d) (\d) (\d)").group(1, 2, 3) == (b"0", b"1", b"0")
    device(peer, "key 4", rb"KEY 1")
    dut.expect(rb"H KEY p 4 65", timeout=5)
    assert host(dut, "leds 0", rb"LEDS (\d)").group(1) == b"1"
    # The layout: keycode 0x2F is '[' on a US keyboard and '@' on a Japanese one.
    host(dut, "layout jp", rb"LAYOUT")
    device(peer, "key 47", rb"KEY 1")
    dut.expect(rb"H KEY p 47 64", timeout=5)
    host(dut, "layout us", rb"LAYOUT")
    device(peer, "key 47", rb"KEY 1")
    dut.expect(rb"H KEY p 47 91", timeout=5)

    # Two CDC ports: port 1 answers each byte plus one, through onSerialData().
    assert host(dut, "ports", rb"PORTS (\d+)").group(1) == b"2"
    assert host(dut, "sser 1 0102fe", rb"SSER (\d)").group(1) == b"1"
    assert serial_rx(dut, 1, 3) == "0203ff"
    assert host(dut, "sser 0 a5c3", rb"SSER (\d)").group(1) == b"1"
    assert serial_rx(dut, 0, 2) == "a5c3"
    device(peer, "send 1 5566", rb"SENT 1")
    assert serial_rx(dut, 1, 2) == "5566"
    # Line coding (57600 7E2) and the control lines, as the device sees them.
    assert host(dut, "sconf 1 57600 7 2 2", rb"SCONF (\d)").group(1) == b"1"
    assert host(dut, "slines 1 1 0", rb"SLINES (\d)").group(1) == b"1"
    m = device(peer, "line 1", rb"LINE 1 (\d+) (\d) (\d) (\d) (\d) (\d)")
    assert m.group(1, 2, 3, 4, 5, 6) == (b"57600", b"7", b"2", b"2", b"1", b"0")
    assert host(dut, "slines 1 0 1", rb"SLINES (\d)").group(1) == b"1"
    m = device(peer, "line 1", rb"LINE 1 (\d+) (\d) (\d) (\d) (\d) (\d)")
    assert m.group(5, 6) == (b"0", b"1")

    # The device goes away and comes back: both events, and it works again.
    device(peer, "replug 300", rb"REPLUG", timeout=5)
    dut.expect(rb"H DISCONNECTED \d+ %s" % peer.id["vid"].encode(), timeout=5)
    dut.expect(rb"H CONNECTED \d+ %s" % peer.id["vid"].encode(), timeout=15)
    wait_device(dut, peer.id["input"])
    assert host(dut, "sser 1 10", rb"SSER (\d)").group(1) == b"1"
    assert serial_rx(dut, 1, 1) == "11"
