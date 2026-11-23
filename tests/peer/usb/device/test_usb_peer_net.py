"""usb.peer-net: the DUT (tests/sketch_support/usb_net - CDC-NCM alone) as a
device, the ESP32-S3 (EspUsbHost) as the host: Ethernet frames both ways."""
import pathlib
import sys

import pytest

sys.path.insert(0, str(pathlib.Path(__file__).parent))
from device_peer import Peer, command, dut_ready, settings, wait_enumerated


def test_usb_peer_net(dut, peers):
    peer = Peer(peers)
    build_id = settings(__file__)["build_id"]
    dut_ready(dut, build_id)
    dev = wait_enumerated(peer, build_id)
    assert {(0x02, 0x0D), (0x0A, 0x00)} <= set(dev["interfaces"])

    assert command(peer, "net", rb"NETOPEN (\d)", timeout=10).group(1) == b"1"
    command(peer, "reset", rb"RESET")
    dut.write("net")
    before = dut.expect(rb"NET (\d+) (\d+) ([0-9A-F]{12})\r?\n", timeout=5)

    # S3 -> DUT, and the DUT's echo back.
    assert command(peer, "netsend", rb"NETSENT (\d)").group(1) == b"1"
    dut.write("net")
    after = dut.expect(rb"NET (\d+) (\d+) ([0-9A-F]{12})\r?\n", timeout=5)
    assert int(after.group(2)) == int(before.group(2)) + 1

    # DUT -> S3.
    dut.write("netsend")
    dut.expect_exact("NETSEND 1", timeout=5)
    m = command(peer, "netstat", rb"NETRX (\d+) (\d+)")
    assert int(m.group(2)) >= 2       # the echo and the DUT's own frame

    # 20 frames back to back: with two transmit blocks, those sent while one
    # is out go together in the next; every one reaches the S3.
    before = int(command(peer, "netstat", rb"NETRX (\d+) (\d+)").group(2))
    dut.write("netburst 20")
    m = dut.expect(rb"NETBURST (\d+) (\d+) (\d+) (\d+) (\d+)\r?\n", timeout=15)
    assert int(m.group(1)) == 20
    assert int(m.group(3)) == 2048 and int(m.group(4)) == 2 and int(m.group(5)) == 2
    sent = int(m.group(2))
    assert sent >= 20
    m = command(peer, "netstat", rb"NETRX (\d+) (\d+)")
    assert int(m.group(2)) - before == 20


def test_usb_peer_net_ip(dut, peers):
    """usb.peer-net-ip (2026-11-23): IP on the same CDC-NCM function - the Network library's NetworkUsbNcm, on the
    series without an Ethernet MAC too (lwIP's NETWORK_LWIP_TINY profile). The DUT starts it at 192.168.7.1 with a UDP
    echo on port 7 (ipstart; lwIP takes the frames from then on); the peer, from 192.168.7.2 over its raw NCM frames,
    asks the address by ARP (the answer from the DUT's end of the link: the function's MAC with the last bit flipped),
    pings it, and sends a UDP datagram that must come back the same. Skipped where the build has no IP (the series
    under 32 KB of RAM: IPSTART 2) or the peer has no IP commands (only the Pico peer has them)."""
    import secrets
    peer = Peer(peers)
    build_id = settings(__file__)["build_id"]
    dut_ready(dut, build_id)
    wait_enumerated(peer, build_id)
    assert command(peer, "net", rb"NETOPEN (\d)", timeout=10).group(1) == b"1"
    dut.write("net")
    mac = dut.expect(rb"NET (\d+) (\d+) ([0-9A-F]{12})\r?\n", timeout=5).group(3).decode()
    dut.write("ipstart")
    m = dut.expect(rb"IPSTART (\d) (\S+)\r?\n", timeout=10)
    if m.group(1) == b"2":
        pytest.skip("n/a: this build has no IP (USB_NET_IP: the series with 32 KB of RAM or more)")
    assert m.group(1) == b"1" and m.group(2) == b"192.168.7.1", f"beginNetwork(): {m.group(0)!r}"
    r = command(peer, "ipping 192.168.7.1", rb"(IPPING (\d) (\d) ([0-9a-fA-F]{12})|UNKNOWN \S+|ERR\S* \S+)", timeout=10)
    if not r.group(1).startswith(b"IPPING"):
        pytest.skip(f"the peer has no IP commands (ipping): {r.group(0)!r}")
    want_mac = f"{mac[:10]}{int(mac[10:], 16) ^ 1:02X}"
    assert r.group(2) == b"1", "no ARP answer for 192.168.7.1"
    assert r.group(4).decode().upper() == want_mac, f"ARP answered {r.group(4).decode()}, want {want_mac}"
    assert r.group(3) == b"1", "no echo reply from 192.168.7.1"
    data = secrets.token_bytes(24).hex()
    u = command(peer, f"ipudp 192.168.7.1 7 {data}", rb"IPUDP (\d) (\S+)", timeout=10)
    assert u.group(1) == b"1" and u.group(2).decode().lower() == data, f"UDP echo: {u.group(0)!r}, sent {data}"
    dut.write("ipstat")
    assert int(dut.expect(rb"IPSTAT (\d+)\r?\n", timeout=5).group(1)) >= 1
