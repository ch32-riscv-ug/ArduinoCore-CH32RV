"""The DUT as USB host (usb_peer_host.ino, CH32RVUsbHost) and the ESP32-S3 as
the device (peer_s3/, EspUsbDevice): helpers shared by the host-role tests.
The runner stages this module beside the test."""
import json
import pathlib
import re
import secrets
import time

S3_VID = "303a"                      # EspUsbDevice's default identity


def settings(test_file):
    return json.loads(pathlib.Path(test_file).with_name("case.json").read_text())


def host(dut, line, reply, timeout=10):
    """Send `line` to the DUT, return the match of `H <reply>` (a regex, bytes)."""
    dut.write(line + "\n")
    return dut.expect(rb"H " + reply + rb"\r?\n", timeout=timeout)


def device(peer, line, reply, timeout=10):
    """Send `line` to the S3, return the match of `S3D <reply>`."""
    peer.write(line + "\n")
    return peer.expect(rb"S3D " + reply + rb"\r?\n", timeout=timeout)


def dut_ready(dut, build_id):
    nonce = secrets.token_hex(4)
    dut.write(f"ready {nonce}\n")
    m = dut.expect(rb"READY " + nonce.encode() + rb" (\S+) (\d) (\S+)\r?\n", timeout=15)
    assert m.group(1).decode() == build_id, "the DUT runs another build"
    assert m.group(2) == b"1", ("CH32RVUsbHost.begin() failed: " + m.group(3).decode() +
                                " (a CH32X035F8U6 / F7P6 / D8U6 or X033 has no USB host)")


def wait_device(dut, product, timeout=20):
    """Poll `dev` until the DUT lists the S3 as `product`; its DEV fields and interfaces."""
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
                            serial=fields[2], interfaces=[(i[3], i[4]) for i in itfs])
        if time.monotonic() > deadline:
            raise AssertionError("the DUT did not enumerate the S3: " + text)
        time.sleep(0.5)
