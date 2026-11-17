"""usb.suspend-resume, usb.remote-wakeup.

The DUT runs tests/sketch_support/usb_power: a CDC port whose configuration declares remote wakeup, counting
CH32RVUsbBusEvent::Suspended / Resumed (onBusEvent()). The host is this PC, which suspends the device through Linux's
runtime power management: power/control "auto" with a short power/autosuspend_delay_ms lets the kernel suspend an
idle device, "on" resumes it. With the CDC tty open, cdc-acm asks for remote wakeup (needs_remote_wakeup), so the
kernel sends SET_FEATURE(DEVICE_REMOTE_WAKEUP) before suspending and the DUT may wake the host's port.

Both sysfs files are root's. The test needs them writable by the test user; without that it skips and says how:

  - a rule (beside 61-ch32rv-test.rules, for the test VID:PID only):
      SUBSYSTEM=="usb", ENV{DEVTYPE}=="usb_device", ATTR{idVendor}=="1209", ATTR{idProduct}=="0001", \
        RUN+="/bin/sh -c 'cd /sys%p/power && chgrp plugdev control autosuspend_delay_ms && chmod g+w control autosuspend_delay_ms'"
  - or once, by hand, for the device as it is now (gone at its next enumeration):
      sudo chgrp plugdev /sys/bus/usb/devices/<topology>/power/{control,autosuspend_delay_ms}
      sudo chmod g+w /sys/bus/usb/devices/<topology>/power/{control,autosuspend_delay_ms}

The margins: the kernel suspends once the device has been idle for the delay (200 ms here) and its hub thread gets to
it - well within 5 s. A device sees suspend after 3 ms of an idle bus and reports it from loop(); a remote wakeup is
1 to 15 ms of K, then the host resumes the port (20 ms of resume signalling, 10 ms recovery): 2 s is far beyond both.
"""
import json
import os
import pathlib
import secrets
import sys
import time

import pytest
import serial

sys.path.insert(0, str(pathlib.Path(__file__).parent))
from usb_host import APP, PcUsbHost

DELAY_MS = 200
RULE = ('SUBSYSTEM=="usb", ENV{DEVTYPE}=="usb_device", ATTR{idVendor}=="1209", ATTR{idProduct}=="0001", '
        'RUN+="/bin/sh -c \'cd /sys%p/power && chgrp plugdev control autosuspend_delay_ms && '
        'chmod g+w control autosuspend_delay_ms\'"')


def pm(dut):
    dut.write("pm\n")
    m = dut.expect(rb"PM (\d+) (\d+) ([01]) ([01]) (\d+)\r?\n", timeout=5)
    return {"suspends": int(m.group(1)), "resumes": int(m.group(2)), "suspended": m.group(3) == b"1",
            "wakeup_enabled": m.group(4) == b"1", "configured": int(m.group(5))}


def wait_status(power, wanted, timeout):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if (power / "runtime_status").read_text().strip() == wanted:
            return True
        time.sleep(0.02)
    return False


def wait_dut(dut, check, timeout):
    deadline = time.monotonic() + timeout
    while True:
        state = pm(dut)
        if check(state) or time.monotonic() >= deadline:
            return state
        time.sleep(0.1)


def test_usb_power_contract(dut, record_property):
    settings = json.loads(pathlib.Path(__file__).with_name("case.json").read_text())
    build_id = settings["build_id"]
    nonce = secrets.token_hex(4)
    dut.write(f"ready {nonce}\n")
    m = dut.expect(rb"READY " + nonce.encode() + rb" (\S+) (\d) (\S+)\r?\n", timeout=15)
    assert m.group(1).decode() == build_id
    assert m.group(2) == b"1", "USBDevice.begin() failed: " + m.group(3).decode()
    host = PcUsbHost.by_serial(settings["target"]["dut_usb_serial"])
    assert host.wait_ids(APP, 10), "application device did not enumerate"
    power = host.sysfs / "power"
    control, delay = power / "control", power / "autosuspend_delay_ms"
    if not (os.access(control, os.W_OK) and os.access(delay, os.W_OK)):
        pytest.skip(f"{control} is not writable by this user: install the rule {RULE} "
                    f"(or chgrp plugdev + chmod g+w the two files once, by hand)")
    old_control, old_delay = control.read_text().strip(), delay.read_text().strip()
    try:
        # usb.suspend-resume: the host suspends the idle device and resumes it; the DUT sees both.
        before = pm(dut)
        assert not before["suspended"]
        delay.write_text(str(DELAY_MS))
        control.write_text("auto")
        assert wait_status(power, "suspended", 5), "the host did not suspend the device (power/runtime_status)"
        state = wait_dut(dut, lambda s: s["suspended"] and s["suspends"] > before["suspends"], 2)
        assert state["suspended"] and state["suspends"] > before["suspends"], f"the DUT did not see the suspend: {state}"
        assert not state["wakeup_enabled"], "remote wakeup enabled with no driver asking for it"
        control.write_text("on")
        assert wait_status(power, "active", 2), "the host did not resume the device"
        state = wait_dut(dut, lambda s: not s["suspended"] and s["resumes"] > before["resumes"], 2)
        assert not state["suspended"] and state["resumes"] > before["resumes"], f"the DUT did not see the resume: {state}"
        assert state["configured"] == before["configured"], "the resume reconfigured the device (a reset, not a resume)"
        record_property("host_suspend_resume", f"{state['suspends'] - before['suspends']} / "
                                               f"{state['resumes'] - before['resumes']}")

        # usb.remote-wakeup: with the tty open the kernel enables remote wakeup before suspending; the DUT wakes
        # the host, and the CDC port works afterwards without a new enumeration.
        with serial.Serial(host.tty(), 115200, timeout=0.2) as port:
            time.sleep(0.3)
            port.reset_input_buffer()
            before = pm(dut)
            control.write_text("auto")
            assert wait_status(power, "suspended", 5), "the host did not suspend the device with its tty open"
            state = wait_dut(dut, lambda s: s["suspended"] and s["wakeup_enabled"], 2)
            assert state["suspended"] and state["wakeup_enabled"], f"suspended without remote wakeup enabled: {state}"
            # Stay awake after the wakeup long enough to see it.
            delay.write_text("10000")
            started = time.monotonic()
            dut.write("wake\n")
            m = dut.expect(rb"WAKE ([01]) (\d+)\r?\n", timeout=10)
            assert m.group(1) == b"1", "remoteWakeup() refused"
            assert wait_status(power, "active", 2), "the remote wakeup did not resume the host's port"
            record_property("remote_wakeup_s", round(time.monotonic() - started, 3))
            state = wait_dut(dut, lambda s: not s["suspended"] and s["resumes"] > before["resumes"], 2)
            assert not state["suspended"] and state["resumes"] > before["resumes"], f"no resume after the wakeup: {state}"
            assert state["configured"] == before["configured"], "the wakeup reconfigured the device"
            payload = secrets.token_bytes(32)
            port.write(payload)
            got = b""
            deadline = time.monotonic() + 3
            while len(got) < len(payload) and time.monotonic() < deadline:
                got += port.read(len(payload) - len(got))
            assert got == payload, "the CDC echo after the wakeup"
    finally:
        control.write_text(old_control)
        delay.write_text(old_delay)
