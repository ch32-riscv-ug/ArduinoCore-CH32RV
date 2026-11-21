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


_ROUTES = re.compile(r"#define CH32RV_SERIAL(\d+)_ROUTES \{ \\\n((?:\s*\{ *\d+, \{[^}]*\}[^\n]*\\\n)+)\}")
_ROUTE = re.compile(r"\{ *(\d+), \{ *(P[A-K]\d+), *(P[A-K]\d+)")


def serial_routes(series):
    """{USART number: [(route, TX pad, RX pad), ...]} from variants/<series>/pins_arduino.h (CH32RV_SERIALn_ROUTES)."""
    header = REPO / "variants" / series / "pins_arduino.h"
    if not header.exists():
        return {}
    out = {}
    for number, body in _ROUTES.findall(header.read_text(encoding="utf-8")):
        out[int(number)] = [(int(k), tx, rx) for k, tx, rx in _ROUTE.findall(body)]
    return out



def bus_pads(series, macros, part=None):
    """The pads variants/<series>/pins_arduino.h gives the CH32RV_<name> macros in `macros` (CH32RV_I2C1_SCL ...) on
    `part`: the pads a sketch's Wire.begin() / SPI.begin() may take. Only `#if defined(CH32RV_PART_...) || ...` is
    decided (by `part`); any other condition, or no part, counts both of its branches."""
    header = REPO / "variants" / series / "pins_arduino.h"
    if not header.exists():
        return set()
    part = (part or "").upper()
    stack, out = [], set()   # each entry: [this branch active, an earlier branch was taken, decided]
    for line in header.read_text(encoding="utf-8").splitlines():
        d = line.strip()
        m = re.match(r"#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)", d)
        if m:
            kind, cond = m.groups()
            parts = re.findall(r"CH32RV_PART_(\w+)", cond)
            decided = bool(part) and kind in ("if", "elif") and bool(parts) and \
                re.fullmatch(r"(?:\s*defined\(CH32RV_PART_\w+\)\s*(?:\|\|)?)+", cond.strip()) is not None
            hit = (part in parts) if decided else True
            if kind in ("if", "ifdef", "ifndef"):
                stack.append([hit, hit and decided, decided])
            elif kind == "elif" and stack:
                top = stack[-1]
                top[0] = (not top[1]) and hit if (decided and top[2]) else True
                top[1] = top[1] or (top[0] and decided)
                top[2] = top[2] and decided
            elif kind == "else" and stack:
                top = stack[-1]
                top[0] = not top[1] if top[2] else True
            elif kind == "endif" and stack:
                stack.pop()
            continue
        if not all(e[0] for e in stack):
            continue
        m = re.match(r"#define CH32RV_(\w+)\s+(P[A-K]\d+)\b", d)
        if m and m.group(1) in macros:
            out.add(m.group(2))
    return out
