"""usb.peer-host-suspend, usb.peer-host-resume, usb.peer-host-remote-wakeup: the DUT (usb_peer_host.ino,
CH32RVUsbHost) as the host suspends its root port with the peer's HID composite on it (host_hid's peer sketches),
resumes it, and is woken by the peer's remote wakeup. What the peer saw is the peer's own count of suspends and
resumes (TinyUSB's callbacks, EspUsbDevice's suspended() as its loop sees it, CH32RVUsbDevice's bus events) and
whether the host enabled its remote wakeup before the suspend; that the bus works again is a CDC echo after each
resume.

A peer, not the PC: the PC's ports suspend only with root and a udev rule (power/control), and this is the host's
side - the device's side of the same bus (CH32RVUsbDevice suspended by a host) waits for a CH32RV peer that can be
the host (TODO.ja.md, usb-peer-power)."""
import pathlib
import secrets
import sys
import time

sys.path.insert(0, str(pathlib.Path(__file__).parent))
from host_peer import Peer, device, dut_ready, host, settings, wait_device

SETTLE_S = 0.5       # the peer suspends after 3 ms of idle bus; its loop sees it some time after


def peer_power(peer):
    m = device(peer, "power", rb"POWER (\d) (\d+) (\d+) (\d)")
    return {"suspended": m.group(1) == b"1", "suspends": int(m.group(2)), "resumes": int(m.group(3)),
            "wakeup_enabled": m.group(4) == b"1"}


def dut_power(dut):
    m = host(dut, "power", rb"POWER (\d) (\d+)")
    return m.group(1) == b"1", int(m.group(2))


def echo(dut):
    payload = secrets.token_bytes(16)
    assert host(dut, "cdc " + payload.hex(), rb"CDC (\w*)").group(1).decode() == payload.hex(), \
        "the CDC echo did not come back after the resume"


def suspend(dut):
    m = host(dut, "suspend", rb"SUSPEND (\d) (\S+)")
    assert m.group(1) == b"1", f"suspend() failed: {m.group(2).decode()}"


def test_usb_peer_host_power(dut, peers, record_property):
    peer = Peer(peers)
    dut_ready(dut, settings(__file__)["build_id"])
    device(peer, "?", rb"READY [01]")
    wait_device(dut, peer.id["hid"])
    device(peer, "?", rb"READY 1")
    echo(dut)
    before = peer_power(peer)
    assert not before["suspended"]

    # usb.peer-host-suspend: the peer suspends, with its remote wakeup enabled by the host first.
    suspend(dut)
    time.sleep(SETTLE_S)
    now = peer_power(peer)
    assert now["suspended"] and now["suspends"] == before["suspends"] + 1, f"the peer did not suspend: {now}"
    assert now["wakeup_enabled"], "the host did not enable the peer's remote wakeup before the suspend"
    assert dut_power(dut) == (True, 0)

    # usb.peer-host-resume: back, and the bus carries data.
    m = host(dut, "resume", rb"RESUME (\d) (\S+)")
    assert m.group(1) == b"1", f"resume() failed: {m.group(2).decode()}"
    time.sleep(SETTLE_S)
    now = peer_power(peer)
    assert not now["suspended"] and now["resumes"] == before["resumes"] + 1, f"the peer did not resume: {now}"
    assert dut_power(dut) == (False, 0)
    echo(dut)

    # usb.peer-host-remote-wakeup: suspended again, the peer wakes the bus; the host resumes the port and says so.
    suspend(dut)
    time.sleep(SETTLE_S)
    assert peer_power(peer)["suspended"]
    assert device(peer, "wake", rb"WAKE (\d)").group(1) == b"1", "the peer refused its remote wakeup"
    started = time.monotonic()
    dut.expect(rb"H WAKEUP 1\r?\n", timeout=5)
    record_property("wakeup_s", round(time.monotonic() - started, 3))
    time.sleep(SETTLE_S)
    now = peer_power(peer)
    assert not now["suspended"] and now["resumes"] == before["resumes"] + 2, f"the peer is not resumed: {now}"
    assert dut_power(dut) == (False, 1)
    echo(dut)
