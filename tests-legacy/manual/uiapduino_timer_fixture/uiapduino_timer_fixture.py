#!/usr/bin/env python3
# /// script
# requires-python = ">=3.9"
# dependencies = ["pyserial>=3.5"]
# ///
"""UIAPduino V1.4 SysTick/TIM waveform HIL through the E132 fixture."""

import argparse
import re
import time

import serial


class Fixture:
    def __init__(self, port: str):
        self.uart = serial.Serial(port, 115200, timeout=0.1)
        time.sleep(2.0)
        self.uart.reset_input_buffer()

    def send(self, data: str):
        self.uart.write(data.encode("ascii"))
        self.uart.flush()

    def expect(self, pattern: str, timeout: float = 4.0):
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

    def dut(self, command: str):
        self.send(f"T{command}\n")

    def edges(self, gpio: int, duration_ms: int):
        self.send(f"E{gpio} {duration_ms}\n")
        match = self.expect(
            rf"EDGE gpio={gpio} elapsed_us=(\d+) rises=(\d+) "
            rf"falls=(\d+) high_us=(\d+)",
            timeout=duration_ms / 1000.0 + 4.0,
        )
        return tuple(int(match.group(index)) for index in range(1, 5))


def run(port: str):
    fixture = Fixture(port)
    try:
        fixture.send("X")
        time.sleep(3.0)
        fixture.uart.reset_input_buffer()
        fixture.send("U")
        fixture.expect(r"UART READY")
        time.sleep(0.05)
        fixture.dut("PING")
        fixture.expect(r"DUT PONG")

        fixture.dut("MILLIS")
        delta = int(fixture.expect(r"DUT MILLIS delta=(\d+)").group(1))
        if not 124 <= delta <= 127:
            raise AssertionError(f"millis delay drifted: {delta}")

        fixture.dut("TIMER")
        timer = fixture.expect(
            r"DUT TIMER started=(\d+) invalidated=(\d+) quiesced=(\d+) "
            r"ticks=(\d+) released=(\d+)"
        )
        started, invalidated, quiesced, ticks, released = (
            int(timer.group(index)) for index in range(1, 6)
        )
        if (started, invalidated, quiesced, released) != (1, 1, 1, 1):
            raise AssertionError(f"timer ownership failure: {timer.group(0)}")
        if not 20 <= ticks <= 45:
            raise AssertionError(f"timer update count out of range: {ticks}")

        fixture.dut("PWM")
        fixture.expect(r"DUT PWM pin=PC3 value=64")
        elapsed, rises, falls, high = fixture.edges(17, 250)
        pwm_duty = high / elapsed
        if not (180 <= rises <= 320 and abs(rises - falls) <= 2 and
                0.15 <= pwm_duty <= 0.35):
            raise AssertionError(
                f"PWM waveform: rises={rises} falls={falls} duty={pwm_duty:.3f}"
            )
        fixture.dut("PWMSTOP")
        fixture.expect(r"DUT PWM stopped")

        fixture.dut("TONE")
        fixture.expect(r"DUT TONE pin=PC0 frequency=1000 duration=400")
        elapsed, rises, falls, high = fixture.edges(5, 250)
        tone_duty = high / elapsed
        if not (180 <= rises <= 320 and abs(rises - falls) <= 2 and
                0.35 <= tone_duty <= 0.65):
            raise AssertionError(
                f"tone waveform: rises={rises} falls={falls} duty={tone_duty:.3f}"
            )
        fixture.dut("TONESTOP")
        fixture.expect(r"DUT TONE stopped")
        print(
            "PASS: timer HIL "
            f"millis={delta} ticks={ticks} pwm_duty={pwm_duty:.3f} "
            f"tone_duty={tone_duty:.3f}"
        )
    finally:
        fixture.uart.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--port",
        default="/run/board-identify/by-id/esp32-d0wd-v3-0070070d9394",
    )
    run(parser.parse_args().port)
