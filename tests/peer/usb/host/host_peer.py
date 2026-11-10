"""The DUT as USB host (usb_peer_host.ino, CH32RVUsbHost) and the peer as the device: an ESP32-S3 (peer_s3/,
EspUsbDevice, lines "S3D ...") or an RP2040 (peer_pico/, TinyUSB's device stack, lines "PD ..."); the bench
record's usb.to says which. Helpers shared by the host-role tests. The runner stages this module beside the
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
                 classes="Pico peer classes", vreq="RP2040", input="Pico peer input", storage="Pico peer storage"),
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
