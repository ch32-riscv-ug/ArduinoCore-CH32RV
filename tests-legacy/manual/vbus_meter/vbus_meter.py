# /// script
# dependencies = ["pyserial"]
# ///
"""The VBUS voltmeter rig: an XY-FZ25 (or FZ35) electronic load with its load
kept off, read over a USB-TTL adapter. See README.ja.md beside this file for
the rig itself, its wiring and what its readings are good for.

As a library (a manual test connects the rig for that run only):

    from loader import load
    vm = load("tests-legacy/manual/vbus_meter/vbus_meter.py", "vbus_meter")
    meter = vm.open_meter()          # TEST_VBUS_METER, or None when unset
    if meter:
        mv = meter.millivolts()

From the command line, to see that the rig answers:

    uv run tests-legacy/manual/vbus_meter/vbus_meter.py /dev/serial/by-id/usb-1a86_USB_Serial-if00-port0

And as a manual test (the same check under pytest):

    cd tests && TEST_VBUS_METER=<port> uv run pytest manual/vbus_meter/vbus_meter.py -s
"""
import os
import re
import sys
import time

import serial

ENV = "TEST_VBUS_METER"
LINE = re.compile(rb"(\d+\.\d+)V,(\d+\.\d+)A,")


class VbusMeter:
    """9600 8N1; commands carry no line end and are answered "sucess" (sic) or
    "fail"; after "start" one line a second: "04.93V,0.00A,0.001Ah,00:00".
    The load is switched off first and never on: this is a voltmeter."""

    def __init__(self, port: str):
        self.ser = serial.Serial(port, 9600, timeout=1.5)
        for c in (b"off", b"start"):
            self.ser.write(c)
            time.sleep(0.3)

    def millivolts(self, settle: float = 2.5) -> int:
        """The first full line after `settle` seconds: a line is only sent once
        a second, and VBUS needs a moment after a change."""
        time.sleep(settle)
        self.ser.reset_input_buffer()
        for _ in range(4):
            m = LINE.match(self.ser.readline())
            if m:
                return round(float(m.group(1)) * 1000)
        raise AssertionError(f"no reading from the meter on {self.ser.port}")

    def close(self):
        self.ser.write(b"stop")
        self.ser.close()


def open_meter():
    """The rig named by TEST_VBUS_METER, or None when it is not connected."""
    port = os.environ.get(ENV)
    return VbusMeter(port) if port else None


def test_meter_reads():
    import pytest
    meter = open_meter()
    if meter is None:
        pytest.skip(f"{ENV} is not set (the rig is not connected)")
    try:
        mv = meter.millivolts(settle=1.5)
        print(f"VBUS {mv} mV")
        assert 0 <= mv <= 30000, mv
    finally:
        meter.close()


if __name__ == "__main__":
    port = sys.argv[1] if len(sys.argv) > 1 else os.environ.get(ENV)
    if not port:
        sys.exit(f"usage: vbus_meter.py <port>  (or set {ENV})")
    m = VbusMeter(port)
    try:
        for _ in range(5):
            print(f"{m.millivolts(settle=0.5)} mV", flush=True)
    finally:
        m.close()
