#!/usr/bin/env python3
# /// script
# requires-python = ">=3.9"
# dependencies = ["pyserial>=3.5"]
# ///
"""UIAPduino V1.4 release HIL test through the E132 ESP32 fixture."""

import argparse
import re
import time

import serial


PINS = [
    ("PA1/A1", 0, (25,)),
    ("PA2/A0", 1, (26,)),
    ("PC0/D2", 2, (5,)),
    ("PC1/SDA", 3, (19,)),
    ("PC2/SCL", 4, (18,)),
    ("PC3/D5", 5, (17,)),
    ("PC4/A2", 6, (33,)),
    ("PC5/SCK", 7, (4, 27)),
    ("PC6/MOSI", 8, (2, 12)),
    ("PC7/MISO", 9, (14, 15)),
    ("PD0/D10", 10, (13,)),
    ("PD2/A3", 12, (32,)),
]
ADC_PINS = [
    ("A0/PA2", 1, 26, "C"),
    ("A1/PA1", 0, 25, "C"),
    ("A2/PC4", 6, 33, "B"),
    ("A3/PD2", 12, 32, "B"),
    ("A5/PD5", 15, 22, "B"),
    ("A6/PD6", 16, 21, "B"),
]


class Fixture:
    def __init__(self, port: str):
        self.uart = serial.Serial(port, 115200, timeout=0.1)
        time.sleep(2.0)
        self.uart.reset_input_buffer()

    def close(self):
        self.uart.close()

    def send(self, data: str):
        self.uart.write(data.encode("ascii"))
        self.uart.flush()

    def expect(self, pattern: str, timeout: float = 4.0) -> re.Match:
        regex = re.compile(pattern, re.IGNORECASE)
        deadline = time.monotonic() + timeout
        seen = []
        while time.monotonic() < deadline:
            raw = self.uart.readline()
            if not raw:
                continue
            line = raw.decode("utf-8", "replace").strip()
            if line:
                print(f"  {line}")
                seen.append(line)
                match = regex.search(line)
                if match:
                    return match
        raise AssertionError(f"timeout waiting for {pattern!r}; seen={seen[-8:]}")

    def pin(self, gpio: int, mode: str):
        self.send(f"P{gpio} {mode}\n")
        self.expect(rf"PIN OK gpio={gpio} mode={mode}")

    def dut(self, command: str):
        self.send(f"T{command}\n")

    def start_uart(self):
        self.send("U")
        self.expect(r"UART READY")


def mask_high(value: str) -> set[int]:
    if value == "-":
        return set()
    return {int(pin) for pin in value.split(",")}


def adc_value(fixture: Fixture, pin: int, gpio: int, mode: str) -> int:
    if gpio not in (21, 22):
        fixture.pin(gpio, mode)
        fixture.dut(f"ADC {pin} 20")
        return int(fixture.expect(rf"DUT ADC pin={pin} avg=(\d+)").group(1))

    # ADCWAIT switches the DUT pad to analog/Hi-Z before the jig drives it.
    fixture.dut(f"ADCWAIT {pin} 500 1500")
    time.sleep(0.12)
    fixture.pin(gpio, mode)
    time.sleep(0.55)  # the sample has happened; reclaim the UART pins
    fixture.start_uart()
    return int(fixture.expect(rf"DUT ADCWAIT pin={pin} value=(\d+)").group(1))


def run(port: str):
    fixture = Fixture(port)
    try:
        print("UART")
        fixture.start_uart()
        # The first line after muxing UART2 may be transport settling.
        fixture.dut("PING")
        fixture.dut("PING")
        fixture.expect(r"DUT PONG")

        print("GPIO input/output")
        for name, pin, gpios in PINS:
            drive = gpios[0]
            fixture.pin(drive, "L")
            fixture.dut(f"DIN {pin} 10")
            fixture.expect(rf"DUT DIN pin={pin} value=0")
            fixture.pin(drive, "H")
            fixture.dut(f"DIN {pin} 10")
            fixture.expect(rf"DUT DIN pin={pin} value=1")
            fixture.pin(drive, "D")

            fixture.dut(f"DOUT {pin} 1")
            fixture.expect(rf"DUT DOUT pin={pin} value=1")
            fixture.send("M")
            high_match = fixture.expect(r"MAP .* high=([0-9,]+|-)")
            observed = mask_high(high_match.group(1))
            if not set(gpios).issubset(observed):
                raise AssertionError(f"{name}: high missing {gpios}, got {observed}")
            fixture.dut(f"DOUT {pin} 0")
            fixture.expect(rf"DUT DOUT pin={pin} value=0")
            fixture.send("M")
            low_match = fixture.expect(r"MAP .* high=([0-9,]+|-)")
            if set(gpios) & mask_high(low_match.group(1)):
                raise AssertionError(f"{name}: did not go low: {low_match.group(0)}")
            print(f"PASS {name}")

        print("ADC low / midpoint / high")
        for name, pin, gpio, midpoint_mode in ADC_PINS:
            low = adc_value(fixture, pin, gpio, "L")
            mid = adc_value(fixture, pin, gpio, midpoint_mode)
            high = adc_value(fixture, pin, gpio, "H")
            if not (low <= 80 and low + 120 < mid < high - 120 and high >= 750):
                raise AssertionError(f"{name}: unexpected ADC values {low}/{mid}/{high}")
            fixture.pin(gpio, "D")
            print(f"PASS {name}: {low}/{mid}/{high}")

        print("I2C master <-> ESP32 slave")
        fixture.send("I")
        fixture.expect(r"I2C READY")
        fixture.dut("I2C")
        fixture.expect(r"DUT I2C status=0 count=4 data=DEADBEEF")
        fixture.send("K")
        fixture.expect(r"I2C STATUS receives=1 length=2 data=1234")

        print("SPI mode 0 full duplex")
        fixture.send("X")
        time.sleep(3.0)
        fixture.uart.reset_input_buffer()
        fixture.start_uart()
        fixture.dut("PING")
        fixture.dut("PING")
        fixture.expect(r"DUT PONG")
        fixture.send("J")
        fixture.expect(r"SPI READY mode=0")
        fixture.dut("SPI")
        fixture.expect(r"DUT SPI data=C35A6996")
        fixture.send("Q")
        fixture.expect(r"SPI STATUS transfers=1 data=31425364")
        print("PASS: release HIL suite")
    finally:
        fixture.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="/dev/ttyUSB0")
    run(parser.parse_args().port)
