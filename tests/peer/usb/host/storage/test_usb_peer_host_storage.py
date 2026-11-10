"""usb.peer-host-storage: the DUT (usb_peer_host.ino, CH32RVUsbHost) as the USB host, the RP2040
(host_storage/peer_pico) as the device: mass storage with two logical units, MIDI, a vendor interface with
requests both ways, and a smart card reader whose card comes and goes.

What usb-peer-host-classes leaves out: the MSC logical units, TEST UNIT READY and REQUEST SENSE on a unit
that is not ready, MIDI beyond note-on (control and program change, pitch bend, SysEx both ways), the
vendor request OUT, and the CCID slot: its status, the card's removal and insertion (NotifySlotChange),
power off and Escape. Same silicon requirement as usb-peer-host."""
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).parent))
from host_peer import Peer, device, dut_ready, host, settings, wait_device


def test_usb_peer_host_storage(dut, peers):
    peer = Peer(peers)
    dut_ready(dut, settings(__file__)["build_id"])
    dev = wait_device(dut, peer.id["storage"])
    assert {(0x08, 0x06), (0x01, 0x03), (0xFF, 0x00), (0x0B, 0x00)} <= set(dev["interfaces"])
    device(peer, "?", rb"READY 1")

    # Mass storage: two units of different sizes, each read where it differs.
    assert host(dut, "mscmaxlun", rb"MSCMAXLUN (\d) (\d+)").group(1, 2) == (b"1", b"1")
    for lun, blocks, product in ((0, 64, b"Pico LUN 0"), (1, 16, b"Pico LUN 1")):
        assert host(dut, f"msclun {lun}", rb"MSCLUN (\d)").group(1) == b"1"
        m = host(dut, "msc", rb"MSC (\d) (\S+) (.+) (\d+) (\d+)", timeout=20)
        assert m.group(1) == b"1" and m.group(3) == product
        assert (int(m.group(4)), int(m.group(5))) == (blocks, 512)
        assert host(dut, "mscraw 5", rb"MSCRAW (\d)", timeout=20).group(1) == b"1"
    # A unit not ready: TEST UNIT READY fails with 02/3A/00 (medium not present), then ready again.
    device(peer, "media 1 0", rb"MEDIA 1 0")
    assert host(dut, "msctur", rb"MSCTUR (\d) (\w\w \w\w \w\w)").group(1, 2) == (b"0", b"02 3a 00")
    device(peer, "media 1 1", rb"MEDIA 1 1")
    assert host(dut, "msctur", rb"MSCTUR (\d) [^\r\n]*").group(1) == b"1"
    # Unit 0 stays ready meanwhile.
    assert host(dut, "msclun 0", rb"MSCLUN (\d)").group(1) == b"1"
    assert host(dut, "msctur", rb"MSCTUR (\d) [^\r\n]*").group(1) == b"1"

    # MIDI, host to device: control change, program change, pitch bend, SysEx (USB-MIDI packets as sent).
    device(peer, "midilast", rb"MIDILAST \w*")
    assert host(dut, "midicc 1 7 100", rb"MIDIOUT (\d)").group(1) == b"1"
    assert host(dut, "midipc 2 5", rb"MIDIOUT (\d)").group(1) == b"1"
    assert host(dut, "midipb 3 8192", rb"MIDIOUT (\d)").group(1) == b"1"
    assert host(dut, "midisx f0010203f7", rb"MIDIOUT (\d)").group(1) == b"1"
    packets = device(peer, "midilast", rb"MIDILAST (\w*)").group(1).decode()
    assert packets == "0bb10764" + "0cc20500" + "0ee30040" + "04f00102" + "0603f700"
    # Device to host: the same kinds, decoded.
    device(peer, "midiraw 0bb40a7f0cc5090006f0f700", rb"MIDIRAW 1")
    dut.expect(rb"H MIDI 11 b4 10 127", timeout=5)
    dut.expect(rb"H MIDI 12 c5 9 0", timeout=5)
    dut.expect(rb"H MIDI 6 f0 247 0", timeout=5)

    # The vendor request OUT, with its value, index and data.
    assert host(dut, "vreqout 0x11 0x1234 0x0002 c0ffee", rb"VREQOUT (\d)").group(1) == b"1"
    assert device(peer, "vlast", rb"VLAST (\d+) (\d+) (\w*)").group(1, 2, 3) == (b"4660", b"2", b"c0ffee")

    # The card reader: a card present, powered, an Escape echoed, powered off.
    m = host(dut, "ccid", rb"CCID (\d) (\w*)", timeout=15)
    assert m.group(1) == b"1"
    assert host(dut, "ccidstatus", rb"CCIDSTATUS (\d) \d+ (\d) (\d)").group(1, 2, 3) == (b"1", b"1", b"1")
    assert host(dut, "ccidesc 0a0b0c", rb"CCIDESC (\d) (\w*)").group(1, 2) == (b"1", b"0a0b0c")
    assert host(dut, "ccidoff", rb"CCIDOFF (\d)").group(1) == b"1"
    assert host(dut, "ccidstatus", rb"CCIDSTATUS (\d) \d+ (\d) (\d)").group(1, 2, 3) == (b"1", b"1", b"0")
    # The card taken out and put back: NotifySlotChange both ways, and the slot follows.
    device(peer, "card 0", rb"CARD 0")
    dut.expect(rb"H CARD out", timeout=5)
    assert host(dut, "ccidpresent", rb"CCIDPRESENT (\d)").group(1) == b"0"
    assert host(dut, "ccid", rb"CCID (\d) \w*", timeout=15).group(1) == b"0"
    device(peer, "card 1", rb"CARD 1")
    dut.expect(rb"H CARD in", timeout=5)
    assert host(dut, "ccidpresent", rb"CCIDPRESENT (\d)").group(1) == b"1"
    assert host(dut, "ccid", rb"CCID (\d) \w*", timeout=15).group(1) == b"1"
