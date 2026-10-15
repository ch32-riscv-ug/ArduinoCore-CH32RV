"""Talking to the ESP32-S3 peer (tests/sketch_support/usb_peer/device/peer_s3) and to the DUT console.

The S3 answers each command with one "S3 ..." line; events (connect,
disconnect, keys, HID / MIDI input) arrive as "S3 ..." lines too, so a reply
is matched by its own keyword, never as "the next line". The runner stages this
module beside the test.
"""
import json
import pathlib
import re
import secrets
import time

DEVICE_VID_PID = ("1209", "0001")       # CH32RVUsbDevice's default identity
ISP_VID_PID = ("1a86", "55e0")          # the CH32X035 factory bootloader


def settings(test_file):
    return json.loads(pathlib.Path(test_file).with_name("case.json").read_text())


def command(peer, line, reply, timeout=10):
    """Send `line`, return the match of `S3 <reply>` (a regex, bytes)."""
    peer.write(line + "\n")
    return peer.expect(rb"S3 " + reply + rb"\r?\n", timeout=timeout)


def dut_ready(dut, build_id):
    nonce = secrets.token_hex(4)
    dut.write(f"ready {nonce}\n")
    m = dut.expect(rb"READY " + nonce.encode() + rb" (\S+) (\d) (\S+)\r?\n", timeout=15)
    assert m.group(1).decode() == build_id, "the DUT runs another build"
    assert m.group(2) == b"1", "USBDevice.begin() failed: " + m.group(3).decode()


def wait_enumerated(peer, build_id, timeout=20):
    """Poll `dev` until the S3 lists this build; the DEV line and its ITF rows."""
    deadline = time.monotonic() + timeout
    while True:
        peer.write("dev\n")
        block = peer.expect(rb"(S3 DEV [^\n]*\n(?:S3 ITF [^\n]*\n)*)?S3 DEVEND (\d+)\r?\n", timeout=10)
        text = (block.group(1) or b"").decode(errors="replace")
        for dev in re.finditer(r"S3 DEV (\d+) (\w{4}) (\w{4}) (\d) (\w{4}) (\w{2}) ([^\n]*)", text):
            product = dev.group(7).split("|")[1] if "|" in dev.group(7) else ""
            if product.rstrip().endswith(build_id):
                itfs = [tuple(int(x, 16) if i >= 3 else int(x) for i, x in enumerate(r.split()))
                        for r in re.findall(r"S3 ITF (\d+ \d+ \d+ \w{2} \w{2} \w{2}) \d", text)]
                return dict(address=int(dev.group(1)), vid=dev.group(2), pid=dev.group(3),
                            speed=int(dev.group(4)), usb=dev.group(5), product=product.strip(),
                            interfaces=[(i[3], i[4]) for i in itfs if i[0] == int(dev.group(1))])
        if time.monotonic() > deadline:
            raise AssertionError("the S3 did not enumerate this build: " + text)
        time.sleep(0.5)
