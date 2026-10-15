"""usb.enumerate, usb.cdc-round-trip, usb.hid-composite, usb.hid-keyboard, usb.midi-round-trip,
usb.net-frames, usb.reconnect, usb.isp-entry-dfu-detach, usb.isp-entry-touch.

The DUT runs tests/sketch_support/usb_contracts; the test host is this PC (PcUsbHost in usb_host.py), which
reaches the DUT's own USB port at the configured dut_usb_topology. A host microcontroller (a Pico on
the bench, later) replaces PcUsbHost and nothing else. The OS HID driver is detached before the
keyboard types, so no input reaches this PC's desktop.
"""
import json
import pathlib
import secrets
import sys
import time

import serial
import usb.util

# The runner stages usb_host.py beside this file; pytest imports tests without adding their directory.
sys.path.insert(0, str(pathlib.Path(__file__).parent))
from usb_host import APP, ISP, PcUsbHost, endpoints, interface


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
    assert {(0x02, 0x02), (0x02, 0x0D), (0x0A, 0x00), (0x03, 0x00), (0x01, 0x03), (0xFE, 0x01)} <= classes

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

    # usb.net-frames: the host binds the NCM interface with the MAC the device names, frames flow
    # both ways (the host's own IPv6 traffic in; a broadcast frame out), without root.
    dut.write("net\n")
    mac = dut.expect(rb"NET \d+ ([0-9A-F]{12})\r?\n", timeout=5).group(1).decode().lower()
    iface = host.net_interface(timeout=10)
    assert iface.address() == ":".join(mac[i:i + 2] for i in range(0, 12, 2))
    assert iface.wait_up(20), "the host did not bring the NCM interface up"
    before = iface.rx_packets()
    dut.write("netsend\n")
    dut.expect_exact("NETSEND 1", timeout=5)
    deadline = time.monotonic() + 5
    while iface.rx_packets() == before and time.monotonic() < deadline:
        time.sleep(0.05)
    assert iface.rx_packets() > before, "the device's frame did not reach the host"
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        dut.write("net\n")
        if int(dut.expect(rb"NET (\d+) ", timeout=5).group(1)) > 0:
            break
        time.sleep(1)
    else:
        raise AssertionError("no frame from the host reached the device")

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
