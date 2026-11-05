"""Which peripherals each series has: the clock enables its variant header names (CH32RV_CLKEN_<name>_ADDR)."""
import pathlib
import re

REPO = pathlib.Path(__file__).resolve().parents[2]


def series_capabilities():
    """{series: {peripheral, ...}} from variants/<series>/pins_arduino.h."""
    return {header.parent.name: set(re.findall(r"CH32RV_CLKEN_([A-Z0-9_]+)_ADDR", header.read_text(encoding="utf-8")))
            for header in sorted((REPO / "variants").glob("*/pins_arduino.h"))}
