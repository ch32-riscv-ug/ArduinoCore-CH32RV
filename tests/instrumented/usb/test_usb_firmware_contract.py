"""usb.dfu-download, usb.dfu-refuse, usb.uf2-drive, usb.uf2-refuse, usb.raw-drive.

The DUT runs tests/sketch_support/usb_firmware: CH32RVUsbDeviceDfu (DFU mode, protocol 2) and
CH32RVUsbDeviceMscFirmwareDisk beside the default DFU runtime. The host sends the DUT's own image back - the .bin this
case built - with the 16 hex digits after "CH32GEN:" changed, and reads them back after the restart: the new digits
can only come from an image that was written, checked, installed and started. The refusals send what must not be
installed (bytes that are not an image, a download abandoned half way, a UF2 for another family) and check the old
digits are still there.

DFU is spoken here with pyusb (DFU 1.1: DNLOAD, GETSTATUS, CLRSTATUS, ABORT), the drive with SCSI over bulk-only
transport (usb_host.py's helpers, the kernel's usb-storage detached): no dfu-util, no mount.

Writes the DUT's program flash several times: the runner builds it with CH32RV_BOOT_HOLD_MS (the case's notes).
The image must fit the staging area the DUT reports (about the flash less the sketch); a part where it does not
(V203 ANY's 32 KB) skips.
"""
import json
import pathlib
import secrets
import struct
import sys
import time

import pytest
import usb.core
import usb.util

sys.path.insert(0, str(pathlib.Path(__file__).parent))
from usb_host import APP, BulkOnly, PcUsbHost, interface

TAG = b"CH32GEN:"
DFU_DNLOAD, DFU_GETSTATUS, DFU_CLRSTATUS, DFU_ABORT = 1, 3, 4, 6
DFU_IDLE, DFU_DNLOAD_IDLE, DFU_MANIFEST, DFU_ERROR = 2, 5, 7, 10
ERR_WRITE = 3
UF2_FAMILY_ERROR = 30


def fw(dut, timeout=15):
    """FW from the DUT, asked again until it answers (the console comes back after a restart)."""
    deadline = time.monotonic() + timeout
    while True:
        dut.write("fw\n")
        try:
            m = dut.expect(rb"FW ([0-9a-f]{16}) (\d+) ([0-9A-F]+) (\d+) (\d+) (\d+) (\d+) (\d+) (\d+) (\d+)\r?\n",
                           timeout=2)
            g = m.groups()
            return {"generation": g[0].decode(), "capacity": int(g[1]), "family": int(g[2], 16),
                    "ram_sectors": int(g[3]), "blocks": int(g[4]), "dfu_status": int(g[5]), "dfu_state": int(g[6]),
                    "disk_error": int(g[7]), "dfu_errors": int(g[8]), "disk_written": int(g[9])}
        except Exception:
            if time.monotonic() >= deadline:
                raise


def with_generation(image, generation):
    at = image.index(TAG) + len(TAG)
    return image[:at] + generation.encode() + image[at + 16:]


def uf2(image, family, payload=256, base=0x08000000):
    """uf2conv.py's layout: 256 bytes a block, the family ID in the header, the last block padded with 0xFF."""
    count = (len(image) + payload - 1) // payload
    blocks = []
    for n in range(count):
        data = image[n * payload:(n + 1) * payload].ljust(payload, b"\xff")
        head = struct.pack("<IIIIIIII", 0x0A324655, 0x9E5D5157, 0x00002000, base + n * payload, payload, n, count,
                           family)
        blocks.append(head + data.ljust(476, b"\0") + struct.pack("<I", 0x0AB16F30))
    return blocks


class Dfu:
    """DFU 1.1 on the DUT's DFU-mode interface (class 0xFE, subclass 1, protocol 2)."""

    def __init__(self, host):
        self.host = host

    def __enter__(self):
        self.dev = self.host.device()
        itf = next(i for i in self.dev.get_active_configuration()
                   if (i.bInterfaceClass, i.bInterfaceSubClass, i.bInterfaceProtocol) == (0xFE, 0x01, 0x02))
        self.itf = itf.bInterfaceNumber
        functional = bytes(itf.extra_descriptors)
        self.transfer = struct.unpack_from("<H", functional, 5)[0]
        return self

    def __exit__(self, *exc):
        usb.util.dispose_resources(self.dev)

    def status(self):
        s = bytes(self.dev.ctrl_transfer(0xA1, DFU_GETSTATUS, 0, self.itf, 6, timeout=5000))
        time.sleep(int.from_bytes(s[1:4], "little") / 1000)
        return s[0], s[4]

    def block(self, number, data):
        self.dev.ctrl_transfer(0x21, DFU_DNLOAD, number, self.itf, data, timeout=5000)
        return self.status()

    def clear(self):
        self.dev.ctrl_transfer(0x21, DFU_CLRSTATUS, 0, self.itf, None, timeout=1000)

    def abort(self):
        self.dev.ctrl_transfer(0x21, DFU_ABORT, 0, self.itf, None, timeout=1000)

    def download(self, image):
        for n, at in enumerate(range(0, len(image), self.transfer)):
            status, state = self.block(n, image[at:at + self.transfer])
            if status or state != DFU_DNLOAD_IDLE:
                return status, state
        self.dev.ctrl_transfer(0x21, DFU_DNLOAD, 0, self.itf, None, timeout=5000)
        return self.status()


def come_back(host, dut, build_id, timeout=30):
    assert host.wait_gone(APP, 10), "the device did not leave the bus to install the image"
    assert host.wait_ids(APP, timeout), "the device did not come back after the install"
    deadline = time.monotonic() + 5
    while build_id not in host.product() and time.monotonic() < deadline:
        time.sleep(0.05)
    return fw(dut)


def test_usb_firmware_contract(dut, record_property):
    here = pathlib.Path(__file__).parent
    settings = json.loads(here.joinpath("case.json").read_text())
    build_id = settings["build_id"]
    nonce = secrets.token_hex(4)
    dut.write(f"ready {nonce}\n")
    m = dut.expect(rb"READY " + nonce.encode() + rb" (\S+) (\d) (\S+)\r?\n", timeout=15)
    assert m.group(1).decode() == build_id
    assert m.group(2) == b"1", "USBDevice.begin() failed: " + m.group(3).decode()
    host = PcUsbHost.by_serial(settings["target"]["dut_usb_serial"])
    assert host.wait_ids(APP, 10), "application device did not enumerate"

    image = next(here.glob("build/*/*.ino.bin")).read_bytes()
    first = fw(dut)
    record_property("image_bytes", len(image))
    record_property("staging_bytes", first["capacity"])
    if len(image) > first["capacity"]:
        pytest.skip(f"the image ({len(image)} B) does not fit the staging area ({first['capacity']} B) on this part")
    assert image.count(TAG) == 1

    # usb.dfu-refuse: bytes that are not an image fail on their first block (errWRITE, dfuERROR), and a download
    # abandoned half way (DFU_ABORT) installs nothing; CLRSTATUS / ABORT bring the interface back to dfuIDLE.
    with Dfu(host) as dfu:
        record_property("dfu_transfer_size", dfu.transfer)
        status, state = dfu.block(0, secrets.token_bytes(dfu.transfer))
        assert (status, state) == (ERR_WRITE, DFU_ERROR), f"a block that is no image: status {status} state {state}"
        dfu.clear()
        assert dfu.status() == (0, DFU_IDLE)
        half = with_generation(image, secrets.token_hex(8))[:max(dfu.transfer, len(image) // 2)]
        for n, at in enumerate(range(0, len(half), dfu.transfer)):
            assert dfu.block(n, half[at:at + dfu.transfer]) == (0, DFU_DNLOAD_IDLE)
        dfu.abort()
        assert dfu.status() == (0, DFU_IDLE)
    time.sleep(1)
    assert fw(dut)["generation"] == first["generation"], "a refused download changed the running image"

    # usb.dfu-download: the image with a new generation, installed and running after the restart.
    generation = secrets.token_hex(8)
    with Dfu(host) as dfu:
        started = time.monotonic()
        status, state = dfu.download(with_generation(image, generation))
        assert (status, state) == (0, DFU_MANIFEST), f"manifestation: status {status} state {state}"
    after = come_back(host, dut, build_id)
    record_property("dfu_download_s", round(time.monotonic() - started, 3))
    assert after["generation"] == generation, "the downloaded image is not the one running"

    # The drive: SCSI straight to the firmware region (its first LBA is the DUT's RAM sector count).
    def drive():
        dev = host.device()
        number = interface(dev, 0x08).bInterfaceNumber
        usb.util.dispose_resources(dev)
        return number

    # usb.uf2-refuse: a UF2 block for another family is refused at once; nothing is installed.
    number = drive()
    with host.claimed([number]) as dev:
        disk = BulkOnly(interface(dev, 0x08))
        blocks = uf2(with_generation(image, secrets.token_hex(8)), after["family"] ^ 0x5A5A5A5A)
        try:
            status, _ = disk.command(struct.pack(">BBIBHB", 0x2A, 0, after["ram_sectors"], 0, 1, 0), data=blocks[0])
        except usb.core.USBError:
            status = -1                          # a stall on the way to the status: refused as well
        assert status != 0, "a UF2 block for another family was accepted"
    state = fw(dut)
    assert state["disk_error"] == UF2_FAMILY_ERROR and state["generation"] == after["generation"]

    # usb.uf2-drive: every block, last first (UF2's blocks say where they go), then the restart.
    generation = secrets.token_hex(8)
    number = drive()
    with host.claimed([number]) as dev:
        disk = BulkOnly(interface(dev, 0x08))
        blocks = uf2(with_generation(image, generation), after["family"])
        started = time.monotonic()
        for k, block in enumerate(reversed(blocks)):
            disk.write(after["ram_sectors"] + k, block)
    after = come_back(host, dut, build_id)
    record_property("uf2_s", round(time.monotonic() - started, 3))
    assert after["generation"] == generation, "the UF2 image is not the one running"

    # usb.raw-drive: the .bin in order, ended by the host's eject (START STOP UNIT, LoEj).
    generation = secrets.token_hex(8)
    raw = with_generation(image, generation)
    raw += b"\xff" * (-len(raw) % 512)
    number = drive()
    with host.claimed([number]) as dev:
        disk = BulkOnly(interface(dev, 0x08))
        for k in range(0, len(raw), 4096):
            disk.write(after["ram_sectors"] + k // 512, raw[k:k + 4096])
        status, _ = disk.command(bytes([0x1B, 0, 0, 0, 0x02, 0]))
        assert status == 0
    after = come_back(host, dut, build_id)
    assert after["generation"] == generation, "the raw image is not the one running"
