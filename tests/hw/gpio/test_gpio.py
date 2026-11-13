"""gpio.external-levels: both drive directions, without a shared DUT input/output oracle, on every line the probe
labels for the port (bench_lines.gpio_lines: `<port>@<pad>` and the probe-only roles)."""
import secrets

import pytest

from instrumented.bench_lines import gpio_lines

from oep_client import core
from oep_client.fixture import Gpio


pytestmark = pytest.mark.ch32_case("gpio")


def test_gpio_contract(dut, oep_host, ch32_target, record_property):
    host = oep_host.host
    gpio = Gpio(host)
    pins = gpio_lines(oep_host, ch32_target)
    if not pins:
        pytest.skip("the probe labels no <port>@<pad> line")
    record_property("lines", " ".join(f"{p['pad']}:ch{p['channel']}" for p in pins))
    core.plan_apply(host, [(gpio.fn, 1, p["channel"]) for p in pins])
    try:
        for p in pins:
            pin, channel = p["pin"], p["channel"]
            gpio.configure(channel, Gpio.INPUT)
            for level in [0, 1, 0, 1, 0]:
                dut.write(f"gpio {pin} out{level}\n")
                dut.expect_exact("GPIO OUT", timeout=10)
                assert gpio.read([channel]) == [level], p["pad"]
            # Release the DUT before the independent probe starts driving.
            dut.write(f"gpio {pin} in\n")
            dut.expect(rb"GPIO IN [01]\r?\n", timeout=10)
            for level in [0, 1] + [secrets.randbelow(2) for _ in range(4)]:
                gpio.configure(channel, Gpio.OUTPUT_HIGH if level else Gpio.OUTPUT_LOW)
                dut.write(f"gpio {pin} in\n")
                dut.expect_exact(f"GPIO IN {level}", timeout=10)
            gpio.configure(channel, Gpio.INPUT)
            record_property(p["pad"], "both directions")
    finally:
        for p in pins:
            gpio.configure(p["channel"], Gpio.INPUT)
            dut.write(f"gpio {p['pin']} in\n")
            dut.expect(rb"GPIO IN [01]\r?\n", timeout=10)
        core.plan_release(host, [gpio.fn])
