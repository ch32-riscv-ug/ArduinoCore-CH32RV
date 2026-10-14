"""usb.webusb-descriptors, usb.ms-os-20, usb.vendor-round-trip, usb.vendor-request, usb.msc-block,
usb.msc-file, usb.ccid-apdu.

The DUT runs tests/sketch_support/usb_classes. The host side (usb_host.py) speaks mass storage (bulk-only transport) and CCID itself, with the kernel
driver detached, so no disk is mounted and no PC/SC stack is involved.
"""
import json
import pathlib
import secrets
import struct
import sys
import time

import usb.core
import usb.util

# The runner stages usb_host.py beside this file; pytest imports tests without adding their directory.
sys.path.insert(0, str(pathlib.Path(__file__).parent))
from usb_host import APP, PcUsbHost, endpoints, interface

GUID = "{0B1C2D3E-4F50-6172-8394-A5B6C7D8E9F0}"
WEBUSB_UUID = bytes.fromhex("38b60834a909a0478bfda0768815b665")
MS_OS_20_UUID = bytes.fromhex("df60ddd88945c74c9cd2659d9e648a9f")


def utf16(text):
    return text.encode("utf-16-le")


class BulkOnly:
    """SCSI over mass-storage bulk-only transport."""

    def __init__(self, itf):
        self.ep_in, self.ep_out = endpoints(itf)
        self.tag = 1

    def command(self, cb, length=0, data=None):
        self.tag += 1
        direction = 0x00 if data is not None else 0x80
        cbw = struct.pack("<4sIIBBB", b"USBC", self.tag, len(data) if data is not None else length,
                          direction, 0, len(cb)) + cb.ljust(16, b"\0")
        self.ep_out.write(cbw, timeout=2000)
        payload = b""
        if data is not None:
            self.ep_out.write(data, timeout=2000)
        elif length:
            while len(payload) < length:
                payload += bytes(self.ep_in.read(length - len(payload), timeout=2000))
        csw = bytes(self.ep_in.read(13, timeout=2000))
        signature, tag, _residue, status = struct.unpack("<4sIIB", csw)
        assert signature == b"USBS" and tag == self.tag, csw
        return status, payload

    def read(self, lba, count=1):
        status, data = self.command(struct.pack(">BBIBHB", 0x28, 0, lba, 0, count, 0), 512 * count)
        assert status == 0
        return data

    def write(self, lba, data):
        status, _ = self.command(struct.pack(">BBIBHB", 0x2A, 0, lba, 0, len(data) // 512, 0), data=data)
        assert status == 0


def fat12_add_file(disk, name, data):
    """Put one file in the root directory of the RAM disk, as a host's FAT driver would."""
    boot = disk.read(0)
    reserved, fats, root_entries, _total, spf = (struct.unpack_from("<H", boot, 14)[0], boot[16],
                                                struct.unpack_from("<H", boot, 17)[0],
                                                struct.unpack_from("<H", boot, 19)[0],
                                                struct.unpack_from("<H", boot, 22)[0])
    fat = bytearray(disk.read(reserved, spf))
    root_lba = reserved + fats * spf
    data_lba = root_lba + (root_entries * 32 + 511) // 512

    def entry(n):
        o = n + n // 2
        v = fat[o] | fat[o + 1] << 8
        return v >> 4 if n & 1 else v & 0xFFF

    cluster = next(n for n in range(2, 4000) if entry(n) == 0)
    o = cluster + cluster // 2
    if cluster & 1:
        fat[o] = (fat[o] & 0x0F) | 0xF0
        fat[o + 1] = 0xFF
    else:
        fat[o] = 0xFF
        fat[o + 1] = (fat[o + 1] & 0xF0) | 0x0F
    for k in range(fats):
        disk.write(reserved + k * spf, bytes(fat))
    disk.write(data_lba + cluster - 2, data.ljust(512, b"\0"))
    root = bytearray(disk.read(root_lba))
    slot = next(i for i in range(0, 512, 32) if root[i] in (0, 0xE5))
    base, ext = name.split(".")
    root[slot:slot + 32] = (base.ljust(8) + ext.ljust(3)).encode() + bytes([0x20]) + bytes(14) + \
        struct.pack("<HI", cluster, len(data))
    disk.write(root_lba, bytes(root))


def ccid_exchange(ep_in, ep_out, seq, kind, data=b"", param=b"\0\0\0"):
    ep_out.write(struct.pack("<BIBB", kind, len(data), 0, seq) + param + data, timeout=2000)
    reply = bytes(ep_in.read(64, timeout=2000))
    length = struct.unpack_from("<I", reply, 1)[0]
    while len(reply) < 10 + length:
        reply += bytes(ep_in.read(64, timeout=2000))
    assert reply[6] == seq, reply
    return reply[0], reply[7], reply[8], reply[10:10 + length]


def test_usb_classes_contract(dut, record_property):
    settings = json.loads(pathlib.Path(__file__).with_name("case.json").read_text())
    build_id = settings["build_id"]
    host = PcUsbHost(settings["target"]["dut_usb_topology"])

    nonce = secrets.token_hex(4)
    dut.write(f"ready {nonce}\n")
    m = dut.expect(rb"READY " + nonce.encode() + rb" (\S+) (\d) (\S+)\r?\n", timeout=15)
    assert m.group(1).decode() == build_id
    assert m.group(2) == b"1", "USBDevice.begin() failed: " + m.group(3).decode()
    assert host.wait_ids(APP, 10), "application device did not enumerate"
    deadline = time.monotonic() + 5
    while not host.product().endswith(build_id) and time.monotonic() < deadline:
        time.sleep(0.05)
    assert host.product().endswith(build_id), "the device is not this build: " + host.product()

    dev = host.device()
    numbers = [i.bInterfaceNumber for i in dev.get_active_configuration()]
    usb.util.dispose_resources(dev)
    with host.claimed(numbers) as dev:
        assert dev.bcdUSB == 0x0210
        itfs = {(i.bInterfaceClass, i.bInterfaceSubClass): i.bInterfaceNumber for i in dev.get_active_configuration()}
        assert {(0xFF, 0), (0x08, 0x06), (0x0B, 0), (0xFE, 0x01)} <= set(itfs)
        dfu_name = usb.util.get_string(dev, next(i for i in dev.get_active_configuration()
                                                 if i.bInterfaceClass == 0xFE).iInterface)
        assert dfu_name == "CH32RV"

        # usb.webusb-descriptors: the BOS carries both platform capabilities; the landing page.
        head = bytes(dev.ctrl_transfer(0x80, 6, 0x0F00, 0, 5))
        bos = bytes(dev.ctrl_transfer(0x80, 6, 0x0F00, 0, struct.unpack_from("<H", head, 2)[0]))
        caps, o = {}, 5
        while o < len(bos):
            caps[bos[o + 4:o + 20]] = bos[o:o + bos[o]]
            o += bos[o]
        webusb, msos = caps[WEBUSB_UUID], caps[MS_OS_20_UUID]
        url = bytes(dev.ctrl_transfer(0xC0, webusb[22], 0, 2, 255))
        assert url[1:3] == bytes([3, 1]) and url[3:] == b"example.com/ch32rv"

        # usb.ms-os-20: WinUSB on the vendor and DFU interfaces, the vendor one with our GUID.
        length, code = struct.unpack_from("<HB", msos, 24)
        ms = bytes(dev.ctrl_transfer(0xC0, code, 0, 7, length))
        assert len(ms) == length == struct.unpack_from("<H", ms, 8)[0]
        subsets = [ms[o + 4] for o in range(10, len(ms) - 4)
                   if ms[o:o + 4] == bytes([8, 0, 2, 0])]
        assert sorted(subsets) == sorted([itfs[(0xFF, 0)], itfs[(0xFE, 0x01)]]), subsets
        assert ms.count(b"WINUSB") == 2
        assert utf16(GUID) in ms and ms.count(utf16("DeviceInterfaceGUIDs")) == 1

        # usb.vendor-request and usb.vendor-round-trip.
        assert bytes(dev.ctrl_transfer(0xC1, 0x10, 0, itfs[(0xFF, 0)], 4)) == b"CH32"
        v_in, v_out = endpoints(interface(dev, 0xFF))
        payload = secrets.token_bytes(200)
        v_out.write(payload, timeout=2000)
        got = b""
        deadline = time.monotonic() + 5
        while len(got) < len(payload) and time.monotonic() < deadline:
            try:
                got += bytes(v_in.read(64, timeout=500))
            except usb.core.USBTimeoutError:
                pass
        assert got == payload

        # usb.msc-block: SCSI identity and capacity, the FAT12 image, a block written and read back.
        disk = BulkOnly(interface(dev, 0x08))
        status, inquiry = disk.command(bytes([0x12, 0, 0, 0, 36, 0]), 36)
        assert status == 0 and inquiry[8:14] == b"CH32RV" and inquiry[16:27] == b"usb-classes"
        status, capacity = disk.command(bytes([0x25]) + bytes(9), 8)
        assert status == 0 and struct.unpack(">II", capacity) == (19, 512)
        boot = disk.read(0)
        assert boot[510:512] == b"\x55\xaa" and boot[54:62] == b"FAT12   " and boot[43:51] == b"CONTRACT"
        scratch = secrets.token_bytes(512)
        disk.write(19, scratch)
        assert disk.read(19) == scratch

        # usb.msc-file: a file the host writes is read back by the sketch's readFile().
        text = f"host {nonce}\r\n".encode()
        fat12_add_file(disk, "HOST.TXT", text)

        # usb.ccid-apdu: the slot change notification, power on with the ATR, one APDU.
        c_in, c_out = endpoints(interface(dev, 0x0B))
        notify = next(e for e in interface(dev, 0x0B) if e.bmAttributes & 3 == 3)
        try:
            assert bytes(notify.read(8, timeout=500))[:2] == bytes([0x50, 0x03])
        except usb.core.USBTimeoutError:
            pass   # already polled away by an earlier enumeration; the slot status below decides
        kind, status, _err, atr = ccid_exchange(c_in, c_out, 1, 0x62)
        assert (kind, status & 0xC3) == (0x80, 0) and atr.startswith(b"\x3b\x8a\x80\x01CH32RVCCID")
        apdu = bytes([0x00, 0xA4, 0x04, 0x00, 0x02, 0x3F, 0x00])
        kind, status, _err, answer = ccid_exchange(c_in, c_out, 2, 0x6F, apdu)
        assert kind == 0x80 and answer == apdu[::-1] + b"\x90\x00"
        kind, status, _err, _ = ccid_exchange(c_in, c_out, 3, 0x63)
        assert kind == 0x81 and status & 3 == 1

    dut.write("file\n")
    dut.expect_exact(f"FILE {len(text)} host {nonce}", timeout=5)
    dut.write("card\n")
    dut.expect_exact("CARD 1 0 1", timeout=5)
