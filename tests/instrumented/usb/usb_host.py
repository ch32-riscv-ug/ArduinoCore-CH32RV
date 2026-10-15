"""The test host side of the USB device contracts, on this PC: sysfs for presence, pyusb and the tty
for traffic. A host microcontroller (a Pico on the bench, later) replaces this module and nothing
else."""
import glob
import os
import pathlib
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

    def devnum(self):
        try:
            return int((self.sysfs / "devnum").read_text())
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

    def net_interface(self, timeout=10):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            names = sorted(glob.glob(str(self.sysfs) + "/*/net/*"))
            if names:
                return NetInterface(os.path.basename(names[0]))
            time.sleep(0.1)
        raise AssertionError("no network interface under " + self.topology)

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


class NetInterface:
    """A network interface the host made for the device (sysfs only: no root needed)."""

    def __init__(self, name):
        self.path = pathlib.Path("/sys/class/net") / name

    def address(self):
        return (self.path / "address").read_text().strip()

    def rx_packets(self):
        return int((self.path / "statistics/rx_packets").read_text())

    def wait_up(self, timeout):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if (self.path / "operstate").read_text().strip() in ("up", "unknown"):
                return True
            time.sleep(0.1)
        return False

