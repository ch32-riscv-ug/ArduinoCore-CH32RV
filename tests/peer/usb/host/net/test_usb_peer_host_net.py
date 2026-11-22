"""usb.peer-host-net: the DUT (usb_peer_host.ino built with USB_HOST_NET, CH32RVUsbHost) as the USB host, the
ESP32-S3 / P4 (host_net/peer_s3, EspUsbDeviceNet) as a CDC-NCM network device: networkOpen() (the block sizes,
SET_INTERFACE, the device's MAC from its string), the device's NETWORK_CONNECTION, and raw Ethernet frames both
ways - one, then 20 back to back - and the IP stack on it (networkAttachNetif(): DHCP from the device, a TCP
echo). Same silicon requirement as usb-peer-host."""
import pathlib
import sys
import time

sys.path.insert(0, str(pathlib.Path(__file__).parent))
from host_peer import Peer, check_path, device, dut_ready, host, settings, wait_device


def dut_stat(dut):
    m = host(dut, "netstat", rb"NETRX (\d+) (\d+) (\d) (\d+) (\d+)")
    return dict(frames=int(m.group(1)), ours=int(m.group(2)), link=int(m.group(3)), dropped=int(m.group(4)),
                malformed=int(m.group(5)))


def peer_ours(peer):
    return int(device(peer, "netstat", rb"NETRX (\d+) (\d+)").group(2))


def wait_for(read, want, timeout=5):
    deadline = time.monotonic() + timeout
    while True:
        got = read()
        if got >= want or time.monotonic() > deadline:
            return got
        time.sleep(0.2)


def test_usb_peer_host_net(dut, peers):
    peer = Peer(peers)
    dut_ready(dut, settings(__file__)["build_id"])
    dev = wait_device(dut, peer.id["net"])
    check_path(dut, dev, __file__)   # on the root port, or behind the record's hub
    assert {(0x02, 0x0D), (0x0A, 0x00)} <= set(dev["interfaces"])

    m = host(dut, "netopen", rb"NETOPEN (\d) (\d) (\w{12}) (\d+) (\d+)", timeout=10)
    assert m.group(1) == b"1", "networkOpen() failed"
    assert m.group(2) == b"2", "not opened as CDC-NCM"
    mac = device(peer, "netmac", rb"NETMAC (\w{12})").group(1).decode()
    assert m.group(3).decode().upper() == mac, "the MAC from the device's string"
    assert int(m.group(4)) > 0 and int(m.group(5)) > 0
    assert wait_for(lambda: dut_stat(dut)["link"], 1) == 1, "no NETWORK_CONNECTION from the device"

    # DUT -> device, then device -> DUT.
    before = peer_ours(peer)
    assert host(dut, "netsend 1", rb"NETSENT (\d+)").group(1) == b"1"
    assert wait_for(lambda: peer_ours(peer), before + 1) == before + 1
    before = dut_stat(dut)["ours"]
    assert device(peer, "netsend 1", rb"NETSENT (\d+)").group(1) == b"1"
    assert wait_for(lambda: dut_stat(dut)["ours"], before + 1) == before + 1

    # 20 back to back each way: every frame arrives once.
    before = peer_ours(peer)
    assert host(dut, "netsend 20", rb"NETSENT (\d+)", timeout=15).group(1) == b"20"
    assert wait_for(lambda: peer_ours(peer), before + 20) == before + 20
    before = dut_stat(dut)["ours"]
    assert device(peer, "netsend 20", rb"NETSENT (\d+)", timeout=15).group(1) == b"20"
    assert wait_for(lambda: dut_stat(dut)["ours"], before + 20) == before + 20
    s = dut_stat(dut)
    assert s["dropped"] == 0 and s["malformed"] == 0, s

    # The IP stack on top (networkAttachNetif()): an address from the device's DHCP server, then TCP both ways.
    # Not where the Network library does not build (the series without an Ethernet MAC): the DUT says 2.
    m = device(peer, "ipstart", rb"IPSTART (\d) (\S+)", timeout=10)
    assert m.group(1) == b"1", "the peer's IP stack did not start"
    server = m.group(2).decode()
    m = host(dut, "netip", rb"NETIP (\d) (\S+)", timeout=20)
    if m.group(1) == b"2":
        return
    assert m.group(1) == b"1", "no address by DHCP over the device"
    assert m.group(2).decode().startswith("192.168.7."), m.group(2)
    data = bytes(range(160)).hex()   # the command line holds up to 400 characters
    m = host(dut, f"tcpecho {server} 7 {data}", rb"TCPECHO (\d) (\w*)", timeout=15)
    assert m.group(1) == b"1" and m.group(2).decode() == data, m.group(0)
