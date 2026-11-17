"""The DUT as USB host (usb_peer_host.ino, CH32RVUsbHost) and the peer as the device: an ESP32-S3 (peer_s3/,
EspUsbDevice, lines "S3D ..."), an RP2040 (peer_pico/, TinyUSB's device stack, lines "PD ...") or another CH32RV
board (peer_ch32rv/, CH32RVUsbDevice, lines "CD ..."); the bench record's usb.to says which. Helpers shared by the host-role tests. The runner stages this module beside the
test."""
import json
import pathlib
import re
import secrets
import time

# How each peer's two device configurations name themselves (its sketches set the strings; the VIDs are the
# stacks' defaults, EspUsbDevice's and Adafruit TinyUSB's).
IDENTITY = {
    "s3": dict(tag=b"S3D ", vid="303a", manufacturer="EspUsbDevice", hid="S3 peer hid", hid_serial="S3PEERHID",
               classes="S3 peer classes", vreq="ESP32S3"),
    "pico": dict(tag=b"PD ", vid="239a", manufacturer="ch32rv-tests", hid="Pico peer hid", hid_serial="PICOPEERHID",
                 classes="Pico peer classes", vreq="RP2040", input="Pico peer input", storage="Pico peer storage",
                 luns=("Pico LUN 0", "Pico LUN 1")),
    # CH32RVUsbDevice's default VID (pid.codes' test VID).
    "ch32rv": dict(tag=b"CD ", vid="1209", manufacturer="ch32rv-tests", hid="CH32RV peer hid",
                   hid_serial="CH32RVPEERHID", classes="CH32RV peer classes", vreq="CH32RV", input="CH32RV peer input",
                   storage="CH32RV peer storage", luns=("CH32RV LUN 0", "CH32RV LUN 1")),
}


class Peer:
    """The one peer of the test (pytest-embedded's `peers`) and how it names itself."""

    def __init__(self, peers):
        ((self.kind, self.serial),) = peers.items()
        self.id = IDENTITY[self.kind]

    def write(self, text):
        self.serial.write(text)

    def expect(self, pattern, timeout):
        """`pattern` (a regex, bytes) after the peer's tag."""
        return self.serial.expect(self.id["tag"] + pattern, timeout=timeout)


def settings(test_file):
    return json.loads(pathlib.Path(test_file).with_name("case.json").read_text())


def link_speed(test_file):
    """The speed the DUT and the peer meet at (1 full, 2 high, 3 SuperSpeed): the runner's (place.py: the slower of
    the two controllers a CH32RV peer's record names), else high speed with the ESP32-P4 on the DUT's USBHS, else full."""
    target = settings(test_file)["target"]
    return target.get("usb_link_speed") or (2 if target.get("usb_peer") == "p4" else 1)


# The hub the record puts between the DUT and the peer (records.USB_HUBS) and the speed it runs at.
HUB_SPEEDS = {"fs": 1, "hs-stt": 2, "hs-mtt": 2}


def check_path(dut, dev, test_file):
    """The peer `dev` (wait_device's) hangs where the record says: from a hub of the record's kind (a high-speed one
    reaches a full-speed peer by split transactions, a full-speed one a low-speed peer by PRE), else on the root port."""
    hub = settings(test_file)["target"].get("usb_peer_hub")
    m = host(dut, f"where {dev['address']}", rb"WHERE (\d+) (\d+) (\d+)")
    parent, speed = int(m.group(2)), int(m.group(3))
    if hub:
        assert parent and speed == HUB_SPEEDS[hub], f"the peer is not behind the record's {hub} hub: {m.group(0)!r}"
    else:
        assert parent == 0, f"the record has no hub, the peer hangs from hub {parent}"


def host(dut, line, reply, timeout=10):
    """Send `line` to the DUT, return the match of `H <reply>` (a regex, bytes)."""
    dut.write(line + "\n")
    return dut.expect(rb"H " + reply + rb"\r?\n", timeout=timeout)


def device(peer, line, reply, timeout=10):
    """Send `line` to the peer, return the match of `<tag><reply>`."""
    peer.write(line + "\n")
    return peer.expect(reply + rb"\r?\n", timeout=timeout)


def dut_ready(dut, build_id):
    nonce = secrets.token_hex(4)
    dut.write(f"ready {nonce}\n")
    m = dut.expect(rb"READY " + nonce.encode() + rb" (\S+) (\d) (\S+)\r?\n", timeout=15)
    assert m.group(1).decode() == build_id, "the DUT runs another build"
    assert m.group(2) == b"1", ("CH32RVUsbHost.begin() failed: " + m.group(3).decode() +
                                " (a CH32X035F8U6 / F7P6 / D8U6 or X033 has no USB host)")


def wait_device(dut, product, timeout=20):
    """Poll `dev` until the DUT lists the peer as `product`; its DEV fields and interfaces."""
    deadline = time.monotonic() + timeout
    while True:
        dut.write("dev\n")
        block = dut.expect(rb"(H DEV [^\n]*\n(?:H ITF [^\n]*\n)*)?H DEVEND (\d+)\r?\n", timeout=10)
        text = (block.group(1) or b"").decode(errors="replace")
        for dev in re.finditer(r"H DEV (\d+) (\w{4}) (\w{4}) (\d) (\w{4}) (\w+) ([^\r\n]*)", text):
            fields = dev.group(7).split("|")
            if len(fields) == 3 and fields[1] == product:
                itfs = [tuple(int(x, 16) if i >= 3 else int(x) for i, x in enumerate(r.split()))
                        for r in re.findall(r"H ITF (\d+ \d+ \d+ \w{2} \w{2} \w{2}) \d", text)]
                return dict(address=int(dev.group(1)), vid=dev.group(2), pid=dev.group(3),
                            speed=int(dev.group(4)), manufacturer=fields[0], product=fields[1],
                            serial=fields[2], interfaces=[(i[3], i[4]) for i in itfs],
                            # (number, alternate, class, subclass, protocol) of each interface
                            itfs=[i[1:] for i in itfs])
        if time.monotonic() > deadline:
            raise AssertionError("the DUT did not enumerate the peer: " + text)
        time.sleep(0.5)
