"""Which peripherals each series has: the clock enables its variant header names (CH32RV_CLKEN_<name>_ADDR), less what
a part of it lacks (the header's `#if defined(CH32RV_PART_<part>) ... #define CH32RV_<instance>_ABSENT 1`)."""
import pathlib
import re

REPO = pathlib.Path(__file__).resolve().parents[2]

_ABSENT = re.compile(r"#if\s+((?:defined\(CH32RV_PART_\w+\)\s*(?:\|\|\s*)?)+)\n#define CH32RV_(\w+?)_ABSENT\b")


def _headers():
    return sorted((REPO / "variants").glob("*/pins_arduino.h"))


def series_capabilities():
    """{series: {peripheral, ...}} from variants/<series>/pins_arduino.h."""
    return {header.parent.name: set(re.findall(r"CH32RV_CLKEN_([A-Z0-9_]+)_ADDR", header.read_text(encoding="utf-8")))
            for header in _headers()}


def _clock_names(instance):
    """The clock-enable names an instance's ABSENT name stands for: SERIALn is USARTn or UARTn."""
    m = re.fullmatch(r"SERIAL(\d+)", instance)
    return {f"USART{m[1]}", f"UART{m[1]}"} if m else {instance}


def part_absent():
    """{series: {part: {peripheral, ...}}}: what each part lacks of its series' peripherals."""
    out = {}
    for header in _headers():
        parts = out.setdefault(header.parent.name, {})
        for cond, instance in _ABSENT.findall(header.read_text(encoding="utf-8")):
            for part in re.findall(r"CH32RV_PART_(\w+)", cond):
                parts.setdefault(part, set()).update(_clock_names(instance))
    return out


def part_capabilities(series, part, caps=None, absent=None):
    """The peripherals `part` of `series` has."""
    caps = series_capabilities() if caps is None else caps
    absent = part_absent() if absent is None else absent
    return set(caps.get(series, set())) - absent.get(series, {}).get((part or "").upper(), set())
