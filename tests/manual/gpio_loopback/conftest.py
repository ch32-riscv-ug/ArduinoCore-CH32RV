"""Turn the environment's pad choice into a header the sketch can include.

Runs at collection, before the dut fixture builds, and arduino-cli compiles the
sketch directory in place - so writing the header here is enough.

    CH32_LOOPBACK_OUT=PA0  CH32_LOOPBACK_IN=PB0
"""
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))

from env_config import pin, write_pin_header  # noqa: E402

HERE = pathlib.Path(__file__).resolve().parent


def pytest_configure(config) -> None:
    pins = {
        "LOOPBACK_OUT": pin("CH32_LOOPBACK_OUT", "PA0"),
        "LOOPBACK_IN": pin("CH32_LOOPBACK_IN", "PB0"),
    }
    path = write_pin_header(HERE, pins)
    print(f"\n{path.name}: " + ", ".join(
        f"{m}={pad} ({n})" for m, (pad, n) in sorted(pins.items())))
