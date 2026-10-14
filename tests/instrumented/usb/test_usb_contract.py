"""usb.enumerate, usb.cdc-round-trip, usb.hid-composite, usb.hid-keyboard, usb.midi-round-trip,
usb.reconnect, usb.isp-entry-dfu-detach, usb.isp-entry-touch.

The DUT runs tests/sketch_support/usb_contracts; the test host is this PC (PcUsbHost below), which
reaches the DUT's own USB port at the configured dut_usb_topology. A host microcontroller (a Pico on
the bench, later) replaces PcUsbHost and nothing else. The OS HID driver is detached before the
keyboard types, so no input reaches this PC's desktop.
"""
import glob
import json
import os
import pathlib
import secrets
import time

import serial
import usb.core
import usb.util

APP = ("1209", "0001")
ISP = ("1a86", "55e0")


class PcUsbHost:
    """The test host side, on this PC: sysfs for presence, pyusb and the tty for traffic."""

    def __init__(self, topology):
        self.topology = topology
        self.sysfs = pathlib.Path("/sys/bus/usb/devices") / topology

    def ids(self):
        try:
            return ((self.sysfs / "idVendor").read_text().strip(), (self.sysfs / "idProduct").read_text().strip())
        except OSError:
            return None

    def wait_ids(self, wanted, timeout):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if self.ids() == wanted:
                return True
            time.sleep(0.02)
        return False

    def wait_gone(self, wanted, timeout):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if self.ids() != wanted:
                return True
            time.sleep(0.02)
        return False

    def device(self):
        bus, ports = self.topology.split("-", 1)
        path = tuple(int(p) for p in ports.split("."))
        for dev in usb.core.find(find_all=True, idVendor=0x1209, idProduct=0x0001):
            if dev.bus == int(bus) and dev.port_numbers == path:
                return dev
        raise AssertionError("application device not found at " + self.topology)

    def product(self):
        try:
            return (self.sysfs / "product").read_text().strip()
        except OSError:
            return ""

    def tty(self, timeout=5):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            nodes = sorted(glob.glob(str(self.sysfs) + "/*/tty/tty*"))
            if nodes:
                node = "/dev/" + os.path.basename(nodes[0])
                if os.access(node, os.R_OK | os.W_OK):
                    return node
            time.sleep(0.05)
        raise AssertionError("no accessible CDC tty under " + self.topology)

    def interfaces(self):
        dev = self.device()
        return [(i.bInterfaceNumber, i.bInterfaceClass, i.bInterfaceSubClass, i.bInterfaceProtocol)
                for i in dev.get_active_configuration()]

    def claimed(self, numbers):
        """Detach the kernel drivers of these interfaces for the duration of a with-block."""
        host = self

        class Claim:
            def __enter__(self):
                self.dev = host.device()
                self.numbers = numbers
                for n in numbers:
                    if self.dev.is_kernel_driver_active(n):
                        self.dev.detach_kernel_driver(n)
                return self.dev

            def __exit__(self, *exc):
                usb.util.dispose_resources(self.dev)
                for n in self.numbers:
                    try:
                        self.dev.attach_kernel_driver(n)
                    except usb.core.USBError:
                        pass

        return Claim()

    def dfu_detach(self):
        dev = self.device()
        itf = next(i.bInterfaceNumber for i in dev.get_active_configuration()
                   if (i.bInterfaceClass, i.bInterfaceSubClass) == (0xFE, 0x01))
        try:
            dev.ctrl_transfer(0x21, 0, 1000, itf, None, timeout=1000)
        except usb.core.USBError:
            pass   # the status stage often reads as a STALL; the reset is what counts
        finally:
            usb.util.dispose_resources(dev)

    def touch1200(self):
        port = serial.Serial(self.tty(), 1200)
        port.close()


def interface(dev, cls, sub=None):
    for i in dev.get_active_configuration():
        if i.bInterfaceClass == cls and (sub is None or i.bInterfaceSubClass == sub):
            return i
    raise AssertionError(f"no interface of class {cls:#x}")


def endpoints(itf):
    return (next(e for e in itf if e.bEndpointAddress & 0x80), next(e for e in itf if not e.bEndpointAddress & 0x80))


def test_usb_contract(dut, record_property):
    settings = json.loads(pathlib.Path(__file__).with_name("case.json").read_text())
    build_id = settings["build_id"]
    host = PcUsbHost(settings["target"]["dut_usb_topology"])

    # usb.enumerate: this build, at this topology, with every declared function.
    nonce = secrets.token_hex(4)
    dut.write(f"ready {nonce}\n")
    m = dut.expect(rb"READY " + nonce.encode() + rb" (\S+) (\d) (\S+)\r?\n", timeout=15)
    assert m.group(1).decode() == build_id
    assert m.group(2) == b"1", "USBDevice.begin() failed: " + m.group(3).decode()
    assert host.wait_ids(APP, 10), "application device did not enumerate"
    deadline = time.monotonic() + 5
    while build_id not in host.product() and time.monotonic() < deadline:
        time.sleep(0.05)
    assert host.product().endswith(build_id), "the device is not this build: " + host.product()
    classes = {(c, s) for _n, c, s, _p in host.interfaces()}
    assert {(0x02, 0x02), (0x0A, 0x00), (0x03, 0x00), (0x01, 0x03), (0xFE, 0x01)} <= classes

    # usb.cdc-round-trip: 64 binary bytes through the echo.
    payload = bytes([0, 255, 85, 170]) + secrets.token_bytes(60)
    with serial.Serial(host.tty(), 115200, timeout=0.2) as port:
        time.sleep(0.2)
        port.reset_input_buffer()
        port.write(payload)
        got = b""
        deadline = time.monotonic() + 5
        while len(got) < len(payload) and time.monotonic() < deadline:
            got += port.read(len(payload) - len(got))
    assert got == payload

    # usb.hid-composite and usb.hid-keyboard: keyboard (ID 1) and vendor (ID 6) on one interface.
    dev = host.device()
    hid = interface(dev, 0x03).bInterfaceNumber
    usb.util.dispose_resources(dev)
    with host.claimed([hid]) as dev:
        descriptor = bytes(dev.ctrl_transfer(0x81, 6, 0x2200, hid, 512))
        ids = [descriptor[k + 1] for k in range(len(descriptor) - 1) if descriptor[k] == 0x85]
        assert ids == [1, 6], f"report IDs {ids}"
        ep_in, ep_out = endpoints(interface(dev, 0x03))
        ep_out.write(bytes([6, 0x41]) + b"vendor" + bytes(55))
        report = bytes(ep_in.read(64, timeout=2000))
        assert report[:8] == bytes([6, 0x42]) + b"vendor"
        dut.write("keys\n")
        keys = [bytes(ep_in.read(64, timeout=2000))[:4] for _ in range(4)]
        dut.expect_exact("KEYS 1", timeout=5)
        assert keys == [bytes([1, 0, 0, 0x04]), bytes([1, 0, 0, 0]), bytes([1, 0, 0, 0x05]), bytes([1, 0, 0, 0])]

    # usb.midi-round-trip: note 64 on channel 1 comes back as note 76.
    dev = host.device()
    numbers = [interface(dev, 0x01, 0x01).bInterfaceNumber, interface(dev, 0x01, 0x03).bInterfaceNumber]
    usb.util.dispose_resources(dev)
    with host.claimed(numbers) as dev:
        ep_in, ep_out = endpoints(interface(dev, 0x01, 0x03))
        ep_out.write(bytes([0x09, 0x90, 0x40, 0x7F]))
        assert bytes(ep_in.read(64, timeout=2000))[:4] == bytes([0x09, 0x90, 0x4C, 0x7F])

    # usb.reconnect: end() + begin() looks like an unplug and a plug to the host.
    dut.write("replug\n")
    assert host.wait_gone(APP, 3), "the device did not leave the bus"
    dut.expect_exact("REPLUG 1", timeout=5)
    assert host.wait_ids(APP, 10), "the device did not come back"

    # usb.isp-entry-*: both entries reach the factory ISP at the same topology; without an upload
    # it returns to this build by itself.
    for name, enter in (("dfu-detach", host.dfu_detach), ("touch", host.touch1200)):
        time.sleep(1)
        started = time.monotonic()
        enter()
        assert host.wait_ids(ISP, 5), f"{name}: the ISP bootloader did not appear"
        record_property(f"isp_entry_{name}_s", round(time.monotonic() - started, 3))
        assert host.wait_ids(APP, 30), f"{name}: the application did not come back"
        deadline = time.monotonic() + 5
        while not host.product().endswith(build_id) and time.monotonic() < deadline:
            time.sleep(0.05)
        assert host.product().endswith(build_id)
