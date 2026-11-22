"""usb.peer-host-media: the DUT (usb_peer_host.ino built with USB_HOST_AUDIO, CH32RVUsbHost) as the USB host, the
ESP32-S3 / P4 (host_media/peer_s3, EspUsbAudioFunction: a UAC1 headset, 48000 Hz mono 16-bit both ways) as the
device: getAudioStreams(), then a second of capture (the device's counter, every packet in order) and a second of
playback (the DUT's counter, checked by the device). Same silicon requirement as usb-peer-host."""
import pathlib
import re
import sys

sys.path.insert(0, str(pathlib.Path(__file__).parent))
from host_peer import Peer, check_path, device, dut_ready, host, settings, wait_device

BYTES_PER_S = 48000 * 2


def test_usb_peer_host_media(dut, peers):
    peer = Peer(peers)
    dut_ready(dut, settings(__file__)["build_id"])
    dev = wait_device(dut, peer.id["media"])
    check_path(dut, dev, __file__)   # on the root port, or behind the record's hub
    assert (0x01, 0x02) in set(dev["interfaces"])
    device(peer, "?", rb"READY 1")

    dut.write("audstreams")
    block = dut.expect(rb"((?:H AUDSTREAM [^\n]*\n)*)H AUDEND (\d+)\r?\n", timeout=10)
    streams = [tuple(r.split()) for r in re.findall(rb"H AUDSTREAM ([^\r\n]*)", block.group(1))]
    assert int(block.group(2)) >= 2, streams
    assert any(s[0] == b"1" and s[1:4] == (b"1", b"16", b"48000") for s in streams), streams   # capture
    assert any(s[0] == b"0" and s[1:4] == (b"1", b"16", b"48000") for s in streams), streams   # playback

    # Capture: a second of the device's counter, in order (one break allowed: the start).
    m = host(dut, "audin 1000", rb"AUDIN (\d) (\d+) (\d+)", timeout=10)
    assert m.group(1) == b"1", "audioInputStart() failed"
    got, breaks = int(m.group(2)), int(m.group(3))
    assert 0.9 * BYTES_PER_S <= got <= 1.1 * BYTES_PER_S, f"{got} bytes in a second"
    assert breaks == 0, f"{breaks} packets lost or out of order"

    # Playback: a second of the DUT's counter, every sample in order at the device.
    device(peer, "audreset", rb"AUDRESET")
    m = host(dut, "audout 1000", rb"AUDOUT (\d) (\d+) (\d+)", timeout=10)
    assert m.group(1) == b"1", "audioOutputStart() failed"
    s = device(peer, "audstat", rb"AUDRX (\d+) (\d+) (\d+)")
    rx, rx_breaks = int(s.group(1)), int(s.group(2))
    assert 0.85 * BYTES_PER_S <= rx <= 1.1 * BYTES_PER_S, f"the device took {rx} bytes in a second"
    # A packet of silence (the start, before the first audioSend(), and each underrun) breaks the counter at most
    # once per sample it holds: 7 at high speed's 125 us, 49 at full speed's 1 ms, plus the restart after it.
    under = int(m.group(3))
    assert rx_breaks <= under * 50 + 1, f"{rx_breaks} breaks at the device, {under} underruns"
    assert under <= 3, f"{under} underruns in a second"
