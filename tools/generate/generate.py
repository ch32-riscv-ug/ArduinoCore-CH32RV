#!/usr/bin/env python3
"""W-4 prototype generator: boards.txt and linker scripts from ch32-device-data tables.

Reads the normalized CSV tables (products.csv) and writes, per configured family:
  - boards.txt            (board entry + menu.pnum entry per part number)
  - variants/<VARIANT>/<ld>  (one MEMORY script per unique flash/sram combination)

Design rules (see docs/research/board-variants-and-menus.ja.md):
  - one board per mirror family, menu.pnum lists every part number
  - deterministic ordering: canonical series order, then part number
  - generated files carry a DO-NOT-EDIT header with the source repo commit
  - no timestamps in output so regeneration is idempotent (CI check mode)

Usage:
  generate.py --tables <ch32-device-data>/tables --platform <platform dir> [--check]
"""
import argparse
import csv
import pathlib
import re
import textwrap
import subprocess
import sys

# Per-family generation config. Values come from verified research:
# march/mabi and startup CSR defines: docs/research/startup-files.ja.md (R-01),
# experiments 0001/0002. Only families proven by the equivalence harness are listed.
# Startup/ISA parameters shared by every series in an EVT family.
# Values come from the equivalence harness table in tests/startup/run_check.sh.
FAMILY = {
    "CH32V003": dict(march="rv32ec_zicsr", mabi="ilp32e", f_cpu="24000000L",
                     defines="-DCH32_MSTATUS_INIT=0x1880 -DCH32_INTSYSCR_INIT=0x3 -DCH32_HIGHCODE",
                     core_defines="-DCH32_GPIO_PORT_WIDTH=8 -DCH32_SYSTICK_64=0 -DCH32_HSI_HZ=24000000 -DCH32_FLASH_LATENCY=0 -DCH32_ADC_BITS=10"),
    "CH32V006": dict(march="rv32emc_zicsr", mabi="ilp32e", f_cpu="24000000L",
                     defines="-DCH32_MSTATUS_INIT=0x1880 -DCH32_INTSYSCR_INIT=0x3",
                     core_defines="-DCH32_GPIO_PORT_WIDTH=8 -DCH32_SYSTICK_64=0 -DCH32_HSI_HZ=24000000 -DCH32_FLASH_LATENCY=1 -DCH32_ADC_BITS=12"),
    "CH32V205": dict(march="rv32imc_zicsr", mabi="ilp32", f_cpu="8000000L",
                     defines="-DCH32_MSTATUS_INIT=0x88 -DCH32_INTSYSCR_INIT=0x7 "
                             "-DCH32_CORECFGR=0x21 -DCH32_CSR_BC1=0x1",
                     core_defines="-DCH32_GPIO_PORT_WIDTH=16 -DCH32_SYSTICK_64=0 -DCH32_HSI_HZ=8000000 -DCH32_FLASH_LATENCY=0 -DCH32_ADC_BITS=12"),
    "CH32V20x": dict(march="rv32imac_zicsr", mabi="ilp32", f_cpu="8000000L",
                     defines="-DCH32_MSTATUS_INIT=0x88 -DCH32_INTSYSCR_INIT=0x3 "
                             "-DCH32_CORECFGR=0x1f",
                     core_defines="-DCH32_GPIO_PORT_WIDTH=16 -DCH32_SYSTICK_64=1 -DCH32_HSI_HZ=8000000 -DCH32_FLASH_LATENCY=0 -DCH32_ADC_BITS=12"),
    "CH32V307": dict(march="rv32imafc_zicsr", mabi="ilp32f", f_cpu="8000000L",
                     defines="-DCH32_MSTATUS_INIT=0x6088 -DCH32_INTSYSCR_INIT=0x0b "
                             "-DCH32_CORECFGR=0x1f",
                     core_defines="-DCH32_GPIO_PORT_WIDTH=16 -DCH32_SYSTICK_64=1 -DCH32_HSI_HZ=8000000 -DCH32_FLASH_LATENCY=0 -DCH32_ADC_BITS=12"),
    "CH32V407": dict(march="rv32imac_zicsr", mabi="ilp32", f_cpu="20000000L",
                     defines="-DCH32_MSTATUS_INIT=0x688 -DCH32_INTSYSCR_INIT=0x07 "
                             "-DCH32_CORECFGR=0x21 -DCH32_CSR_BC1=0x01 -DCH32_CSR805_CLR=0x100",
                     core_defines="-DCH32_GPIO_PORT_WIDTH=16 -DCH32_SYSTICK_64=0 -DCH32_HSI_HZ=20000000 -DCH32_FLASH_LATENCY=1 -DCH32_ADC_BITS=12"),
    "CH32X035": dict(march="rv32imac_zicsr", mabi="ilp32", f_cpu="48000000L",
                     defines="-DCH32_MSTATUS_INIT=0x88 -DCH32_INTSYSCR_INIT=0x3 "
                             "-DCH32_CORECFGR=0x1f",
                     core_defines="-DCH32_GPIO_PORT_WIDTH=24 -DCH32_SYSTICK_64=1 -DCH32_HSI_HZ=48000000 -DCH32_FLASH_LATENCY=2 -DCH32_ADC_BITS=12"),
    "CH32X315": dict(march="rv32imafc_zicsr", mabi="ilp32f", f_cpu="20000000L",
                     defines="-DCH32_MSTATUS_INIT=0x6088 -DCH32_INTSYSCR_INIT=0x07 "
                             "-DCH32_CORECFGR=0x123703E1 -DCH32_CSR_BC1=0x01",
                     core_defines="-DCH32_GPIO_PORT_WIDTH=16 -DCH32_SYSTICK_64=0 -DCH32_HSI_HZ=20000000 -DCH32_FLASH_LATENCY=1 -DCH32_ADC_BITS=12"),
    # CH32V103's table is a jump table and its startup never writes csr 0x804.
    "CH32V103": dict(march="rv32imac_zicsr", mabi="ilp32", f_cpu="8000000L",
                     defines="-DCH32_MSTATUS_INIT=0x88 -DCH32_MTVEC_MODE=1",
                     core_defines="-DCH32_GPIO_PORT_WIDTH=16 -DCH32_SYSTICK_64=0 "
                                  "-DCH32_HSI_HZ=8000000 -DCH32_FLASH_LATENCY=0 "
                                  "-DCH32_ADC_BITS=12"),
    "CH32L103": dict(march="rv32imac_zicsr", mabi="ilp32", f_cpu="8000000L",
                     defines="-DCH32_MSTATUS_INIT=0x88 -DCH32_INTSYSCR_INIT=0x3 "
                             "-DCH32_CORECFGR=0x1f",
                     core_defines="-DCH32_GPIO_PORT_WIDTH=16 -DCH32_SYSTICK_64=1 -DCH32_HSI_HZ=8000000 -DCH32_FLASH_LATENCY=0 -DCH32_ADC_BITS=12"),
    "CH32M030": dict(march="rv32imc_zicsr", mabi="ilp32", f_cpu="8000000L",
                     defines="-DCH32_MSTATUS_INIT=0x88 -DCH32_INTSYSCR_INIT=0x3 "
                             "-DCH32_CORECFGR=0x21 -DCH32_CSR_BC1=0x1",
                     core_defines="-DCH32_GPIO_PORT_WIDTH=16 -DCH32_SYSTICK_64=0 -DCH32_HSI_HZ=8000000 -DCH32_FLASH_LATENCY=0 -DCH32_ADC_BITS=12"),
    # Excluded, same reason as tests/startup/: CH32H417 boots via loadcode.
}

# One board per silicon series, so the board name matches the chip marking.
# Whether a series can be flashed is not configured here: it follows from
# whether probe-rs has a target for it (tools/index/probe_rs_targets.csv).
# Series it does not cover are still built - they guard the core against
# ISA/CSR regressions - and are labelled "[compile only]" in the menu.
SERIES_CONFIG = {
    "CH32V003": dict(family="CH32V003", vectors="v003"),
    "CH32V002": dict(family="CH32V006", vectors="v00x"),
    "CH32V004": dict(family="CH32V006", vectors="v00x"),
    "CH32V005": dict(family="CH32V006", vectors="v00x"),
    "CH32V006": dict(family="CH32V006", vectors="v00x"),
    "CH32V007": dict(family="CH32V006", vectors="v00x"),
    "CH32M007": dict(family="CH32V006", vectors="v00x"),
    "CH32V103": dict(family="CH32V103", vectors="v103"),
    "CH32V203": dict(family="CH32V20x", vectors="v20x_d6"),
    "CH32V208": dict(family="CH32V20x", vectors="v20x_d8w"),
    "CH32V303": dict(family="CH32V307", vectors="v307_d8"),
    "CH32V305": dict(family="CH32V307", vectors="v307_d8"),
    "CH32V307": dict(family="CH32V307", vectors="v307_d8c"),
    "CH32V317": dict(family="CH32V307", vectors="v307_d8c"),
    "CH32X033": dict(family="CH32X035", vectors="x035"),
    "CH32X035": dict(family="CH32X035", vectors="x035"),
    "CH32L103": dict(family="CH32L103", vectors="l103"),
    "CH32M103": dict(family="CH32L103", vectors="l103"),
    "CH32V205": dict(family="CH32V205", vectors="v205"),
    "CH32V407": dict(family="CH32V407", vectors="v4x7"),
    "CH32V467": dict(family="CH32V407", vectors="v4x7"),
    "CH32X305": dict(family="CH32X315", vectors="x3x5"),
    "CH32X315": dict(family="CH32X315", vectors="x3x5"),
    "CH32M030": dict(family="CH32M030", vectors="m030"),
}

# CH32V203CCT6 is a CH32V205 die sold under a V203 part number: it ships in the
# CH32V205 EVT repository and needs the V205 startup, so it belongs to that board.
SKU_BOARD_OVERRIDE = {"CH32V203CCT6": "CH32V205"}

INTERRUPTS_CSV = pathlib.Path(__file__).parent / "interrupts" / "interrupts.csv"

MENU_HEADER = "menu.pnum=Part Number\n"


def source_commit(tables_dir: pathlib.Path) -> str:
    try:
        out = subprocess.run(["git", "-C", str(tables_dir), "rev-parse", "HEAD"],
                             capture_output=True, text=True, check=True)
        return out.stdout.strip()
    except Exception:
        return "unknown"


def gen_header(commit: str) -> str:
    return ("# DO NOT EDIT - machine generated by tools/generate/generate.py\n"
            "# source: ch32-device-data tables @ git " + commit + "\n"
            "# Regenerate: generate.py --tables <ch32-device-data>/tables "
            "--platform <platform dir>\n")


def ld_header(commit: str) -> str:
    return ("/* DO NOT EDIT - machine generated by tools/generate/generate.py\n"
            " * source: ch32-device-data tables @ git " + commit + " */\n")


def kb(n: int) -> str:
    return str(n // 1024)


def load_interrupts():
    """(entries, forms): variant -> handler list, variant -> "word"|"jump"."""
    table: dict = {}
    forms: dict = {}
    with open(INTERRUPTS_CSV, newline="", encoding="utf-8") as f:
        rows = [r for r in csv.DictReader(
            line for line in f if not line.startswith("#"))]
    for r in rows:
        table.setdefault(r["variant"], []).append(r["handler"] or None)
        forms[r["variant"]] = r["form"]
    return table, forms


def gen_vectors(variant: str, entries: list, form: str, commit: str) -> str:
    """Emit the crt0 vector include for one startup variant."""
    out = [
        "/* DO NOT EDIT - machine generated by tools/generate/generate.py",
        f" * source: tools/generate/interrupts/interrupts.csv (variant {variant})",
        " * Interrupt vector map. Slot 0 (reset) is emitted by crt0_ch32.S;",
        " * this file starts at slot 1. Verified against the EVT startup",
        " * sources by tests/startup/ on every PR. */",
    ]
    # A jump-instruction table (CH32V103) needs CH32_JMP; crt0 emits `j name`
    # for it and selects mtvec mode 1.
    macro = "CH32_JMP" if form == "jump" else "CH32_IRQ"
    width = max((len(h) for h in entries if h), default=0)
    for slot, handler in enumerate(entries, start=1):
        if handler is None:
            body = "    CH32_RSV"
            pad = " " * max(1, 9 + width + 1 - len(body) + 4)
            out.append(f"{body}{pad}/* {slot:3d} reserved */")
        else:
            body = f"    {macro} {handler}"
            pad = " " * max(1, 9 + width + 1 - len(body) + 4 + 9)
            out.append(f"{body}{pad}/* {slot:3d} */")
    return "\n".join(out) + "\n"


# ---------------------------------------------------------------- pin maps
# ADR-0010: pin = (port << 5) | bit. The pad set is the union over every part
# number in the series, so one header serves the ANY entry and all SKUs.
PORTS = "ABCDEF"
# Pads whose datasheet name is a primary function ("OSC_IN"); pin_functions.csv
# carries the port name as a route-less signal row.
PAD_PORT_RE = re.compile(r"^P([A-F])(\d+)")
PORT_SIGNAL_RE = re.compile(r"^P([A-F])(\d+)$")
# ADC_IN0 / ADC1_IN0 (most series) and A0 (V003/X033/X035 datasheets).
ADC_LONG_RE = re.compile(r"^ADC(\d*)_IN(\d+)$")
ADC_SHORT_RE = re.compile(r"^A(\d+)$")

# Pads that are GPIO-shaped in pins.csv but are not GPIO port bits at all
# (dedicated analog/RF/PHY balls). Excluded from the pin map on purpose.
# NRST is here only for CH32V103, whose pins.csv marks it gpio but gives it no
# port name. Other families expose their reset pin as a normal pad (PD7 on
# V003), so this is a data gap rather than a hardware difference - if the pin
# turns out to be usable, device-data should name its port (docs/todo.ja.md).
NON_PORT_PADS = {"ANT", "HO3", "ISP1", "LED0", "LED1",
                 "MDITP", "MDITN", "MDIRP", "MDIRN", "NRST"}

# Register bits that are NOT harmless to write even though no pin carries them
# (ADR-0010 #4 makes unbonded pads harmless; these are the exception).
# Keyed by ch32-device-data errata id, which generate.py verifies still exists.
UNUSABLE_PADS = {
    "x035-pc10-pc17-bonded": {
        "series": ("CH32X033", "CH32X035"),
        "pads": (("C", 10), ("C", 11)),
        "note": ("PC10/PC11 have no pin of their own but are internally bonded "
                 "to PC17/PC16, so writing them drives a real pin."),
    },
}


def load_pin_tables(tables: pathlib.Path):
    """(pads, adc) keyed by part number.

    pads: part -> sorted [(port, bit)]
    adc:  part -> {channel: (port, bit)} for ADC1 only (see below)
    """
    with open(tables / "pin_functions.csv", newline="", encoding="utf-8") as f:
        functions = list(csv.DictReader(f))
    with open(tables / "pins.csv", newline="", encoding="utf-8") as f:
        pinrows = list(csv.DictReader(f))

    # pad -> port name, for pads named after their primary function
    alias = {}
    for r in functions:
        if r["route"] == "" and PORT_SIGNAL_RE.match(r["signal"]):
            alias.setdefault((r["part_number"], r["pad"]), r["signal"])

    def resolve(part: str, pad: str):
        m = PAD_PORT_RE.match(pad)
        if not m:
            a = alias.get((part, pad))
            m = PORT_SIGNAL_RE.match(a) if a else None
        return (m.group(1), int(m.group(2))) if m else None

    pads: dict[str, set] = {}
    unresolved = set()
    for r in pinrows:
        if r["kind"] not in ("gpio", "analog"):
            continue
        got = resolve(r["part_number"], r["pad"])
        if got is None:
            if r["pad"] not in NON_PORT_PADS:
                unresolved.add((r["part_number"], r["pad"]))
            continue
        pads.setdefault(r["part_number"], set()).add(got)

    # analogRead() drives ADC1; parts with several ADCs repeat the same channel
    # numbers on other instances, which would make A<n> ambiguous.
    adc: dict[str, dict] = {}
    for r in functions:
        m = ADC_LONG_RE.match(r["signal"])
        if m:
            instance, channel = int(m.group(1) or 1), int(m.group(2))
        else:
            m = ADC_SHORT_RE.match(r["signal"])
            if not m:
                continue
            instance, channel = 1, int(m.group(1))
        if instance != 1:
            continue
        got = resolve(r["part_number"], r["pad"])
        if got is None:
            continue
        prev = adc.setdefault(r["part_number"], {}).setdefault(channel, got)
        if prev != got:
            raise SystemExit(f"{r['part_number']}: ADC1 channel {channel} maps to "
                             f"both {prev} and {got}")
    return pads, adc, unresolved


def load_errata_ids(tables: pathlib.Path) -> set:
    with open(tables / "errata.csv", newline="", encoding="utf-8") as f:
        return {r["id"] for r in csv.DictReader(f)}


def pad_name(port: str, bit: int) -> str:
    return f"P{port}{bit}"


# USART signal naming is not normalized in device-data (see docs/todo.ja.md):
# V003 says UTX/URX, M030 says UART_TX/UART_RX, X033/X035 say TX1/RX1,
# everyone else says USART1_TX/USART1_RX. Map them onto (index, "TX"|"RX").
UART_SIGNAL_RE = [
    (re.compile(r"^USART(\d+)_(TX|RX)$"), lambda m: (int(m.group(1)), m.group(2))),
    (re.compile(r"^UART(\d+)_(TX|RX)$"),  lambda m: (int(m.group(1)), m.group(2))),
    (re.compile(r"^(TX|RX)(\d+)$"),       lambda m: (int(m.group(2)), m.group(1))),
    (re.compile(r"^U(TX|RX)$"),            lambda m: (1, m.group(1))),
    (re.compile(r"^UART_(TX|RX)$"),        lambda m: (1, m.group(1))),
]
# Route preference: the first one present wins. Families that expose no
# "default" route (V205/X305/X315) only carry af-N alternate-function numbers.
# USART instances the core can drive: base address and whether the peripheral
# hangs off APB1. UART6..8 sit at a different offset and use different RCC bits,
# so they are out of scope for now (see docs/todo.ja.md).
SERIAL_BASES = {1: "CH32_USART1_BASE", 2: "CH32_USART2_BASE",
                3: "CH32_USART3_BASE", 4: "CH32_USART4_BASE",
                5: "CH32_USART5_BASE"}
UART_ROUTE_ORDER = ("default", "main", "af-1", "af-2", "remap-1")


# AFIO remap. device-data gives the controlling field per series and selector;
# the bit list can be non-contiguous (CH32V003 USART1_REMAP is bits 2 and 21),
# so a value is spread over the listed positions, least significant bit first.
REMAP_SELECTOR_RE = re.compile(r"^afio-u(?:s)?art(\d+)-(?:rm|remap)$")
CH32_AFIO_PCFR1_OFFSET = 0x04


def load_remap_fields(tables: pathlib.Path) -> dict:
    """(series, usart index) -> [bit positions], for AFIO PCFR1 only."""
    with open(tables / "remap_fields.csv", newline="", encoding="utf-8") as f:
        rows = list(csv.DictReader(f))
    out: dict = {}
    for r in rows:
        m = REMAP_SELECTOR_RE.match(r["selector"])
        if not m or r["controller"] != "afio" or r["register"] != "PCFR1":
            continue
        bits = [int(b) for b in r["bits"].split(";") if b != ""]
        if bits:
            out[(r["series"], int(m.group(1)))] = bits
    return out


def remap_mask_value(bits: list, value: int):
    mask = 0
    val = 0
    for i, bit in enumerate(bits):
        mask |= 1 << bit
        if (value >> i) & 1:
            val |= 1 << bit
    return mask, val


def route_remap_value(route: str):
    """AFIO field value for a pin_functions route, or None if the route is not
    an AFIO remap (the af-N families use a per-pin selector instead)."""
    if route in ("default", "main"):
        return 0
    if route.startswith("remap-"):
        return int(route.split("-", 1)[1])
    return None


def load_uart_pins(tables: pathlib.Path) -> dict:
    """part -> {(usart index, route): {"TX": (port, bit), "RX": (port, bit)}}.

    TX and RX are kept per route: several families expose a USART only through
    alternate-function routes, and pairing a TX from one route with an RX from
    another would name two pins that cannot be active at the same time.
    """
    with open(tables / "pin_functions.csv", newline="", encoding="utf-8") as f:
        functions = list(csv.DictReader(f))
    out: dict = {}
    for r in functions:
        for pattern, extract in UART_SIGNAL_RE:
            m = pattern.match(r["signal"])
            if m:
                index, direction = extract(m)
                break
        else:
            continue
        if r["route"] not in UART_ROUTE_ORDER:
            continue
        m = PAD_PORT_RE.match(r["pad"])
        if not m:
            continue
        out.setdefault(r["part_number"], {}).setdefault(
            (index, r["route"]), {})[direction] = (m.group(1), int(m.group(2)))
    return out


def choose_uarts(series: str, parts: list, uarts: dict, handler_of: dict,
                 remap: dict) -> dict:
    """Pick one route per USART for the whole series.

    A board is a series (ADR-0005) and its default menu entry is ANY, so the
    pins have to be chosen for the series rather than for one package. Prefer
    the route that reaches the most part numbers; ties go to the earliest route
    in UART_ROUTE_ORDER. Parts that do not bond the chosen pins simply have no
    output there, which is reported in the generated header.
    """
    chosen: dict = {}
    indices = {i for pn in parts for (i, _r) in uarts.get(pn, {})}
    for index in sorted(indices):
        if index not in handler_of or index not in SERIAL_BASES:
            continue
        best = None
        for route in UART_ROUTE_ORDER:
            pads_by_part = {}
            for pn in parts:
                entry = uarts.get(pn, {}).get((index, route))
                if entry and {"TX", "RX"} <= set(entry):
                    pads_by_part[pn] = (entry["TX"], entry["RX"])
            if not pads_by_part:
                continue
            variants = set(pads_by_part.values())
            if len(variants) != 1:
                continue   # the route moves between packages: unusable for ANY
            coverage = len(pads_by_part)
            # A route the core cannot actually select is worse than a smaller
            # one it can: non-default routes need an AFIO field, and device-data
            # does not have one for every series (X033/X035 USART1, for
            # example), while the af-N families use a different mechanism.
            value = route_remap_value(route)
            programmable = value == 0 or (value is not None and
                                          (series, index) in remap)
            score = (programmable, coverage, -UART_ROUTE_ORDER.index(route))
            if best is None or score > best[0]:
                best = (score, coverage, route, next(iter(variants)))
        if best:
            chosen[index] = best[1:]
    return chosen


# probe-rs target names, extracted from `probe-rs chip list` (see
# tools/index/probe_rs_targets.csv). `probe-rs download` refuses an ambiguous
# name, so every menu entry gets a concrete part number.
PROBE_RS_CSV = pathlib.Path(__file__).parent.parent / "index" / "probe_rs_targets.csv"


def load_probe_rs_targets() -> set:
    with open(PROBE_RS_CSV, newline="", encoding="utf-8") as f:
        rows = csv.DictReader(line for line in f if not line.startswith("#"))
        return {r["chip"] for r in rows}


def probe_rs_chip(part: str, series: str, ordered_parts: list, known: set):
    """The --chip value for one menu entry.

    An exact match wins. Otherwise fall back to another part of the same series
    that probe-rs does know: the flash algorithm is per family, and the memory
    bounds that matter are already enforced by upload.maximum_size. For ANY the
    fallback is the smallest part in the series, which is what ANY declares.
    """
    if part in known:
        return part
    for candidate in ordered_parts:
        if candidate in known:
            return candidate
    prefix = [c for c in sorted(known) if c.startswith(series)]
    return prefix[0] if prefix else None


def gen_irqns(variant: str, entries: list) -> str:
    """Interrupt numbers for one startup variant, derived from the same table
    that builds the vector list: slot index == IRQ number."""
    out = [
        "/* DO NOT EDIT - machine generated by tools/generate/generate.py",
        f" * source: tools/generate/interrupts/interrupts.csv (variant {variant})",
        " * Slot index in the vector table == interrupt number for PFIC. */",
        "#pragma once",
        "",
    ]
    width = max((len(h) for h in entries if h), default=0)
    for slot, handler in enumerate(entries, start=1):
        if handler:
            name = handler.removesuffix("_Handler").removesuffix("_IRQHandler")
            out.append(f"#define CH32_IRQN_{name:<{width}} {slot}")
    return "\n".join(out) + "\n"


# EXTI vectors are grouped two different ways: EXTI7_0 / EXTI15_8 on the small
# parts, EXTI0..EXTI4 plus EXTI9_5 / EXTI15_10 elsewhere. Derive both the
# handler names and the lines each one covers from the vector table.
EXTI_RANGE_RE = re.compile(r"^EXTI(\d+)_(\d+)_IRQHandler$")
EXTI_SINGLE_RE = re.compile(r"^EXTI(\d+)_IRQHandler$")
# Only the lines that reach AFIO_EXTICR, i.e. pin bits 0..15. X033/X035 route
# bits 16..23 through EXTI25_16, which needs EXTICR words this core does not
# program yet (docs/todo.ja.md).
EXTI_LINE_MASK = 0xFFFF


# Timer capture/compare signal naming, like the USART case, is not normalized:
# TIM1_CH1 on most families, T1CH1 on V003, T1C1 on X033/X035. Complementary
# outputs (…N) are skipped: driving one needs the break/dead-time setup that
# analogWrite() has no way to express.
PWM_SIGNAL_RE = [
    re.compile(r"^TIM(\d+)_CH(\d+)$"),
    re.compile(r"^T(\d+)CH(\d+)(?:ETR)?$"),
    re.compile(r"^T(\d+)C(\d+)$"),
]
# Timers at a known base with a known clock-enable bit. TIM1 is the advanced
# one on APB2; TIM2/TIM3 are general purpose on APB1.
PWM_TIMERS = (1, 2, 3)


def load_pwm_pins(tables: pathlib.Path) -> dict:
    """part -> {(port, bit): (timer, channel)} for the default route only."""
    with open(tables / "pin_functions.csv", newline="", encoding="utf-8") as f:
        functions = list(csv.DictReader(f))
    out: dict = {}
    for r in functions:
        if r["route"] not in ("default", "main"):
            continue
        for pattern in PWM_SIGNAL_RE:
            m = pattern.match(r["signal"])
            if m:
                break
        else:
            continue
        timer, channel = int(m.group(1)), int(m.group(2))
        if timer not in PWM_TIMERS or not 1 <= channel <= 4:
            continue
        pm = PAD_PORT_RE.match(r["pad"])
        if not pm:
            continue
        out.setdefault(r["part_number"], {})[(pm.group(1), int(pm.group(2)))] = \
            (timer, channel)
    return out


def gen_exti(variant: str, entries: list) -> str:
    groups = []
    for name in entries:
        if not name:
            continue
        m = EXTI_RANGE_RE.match(name)
        if m:
            hi, lo = int(m.group(1)), int(m.group(2))
            mask = ((1 << (hi + 1)) - 1) & ~((1 << lo) - 1)
        else:
            m = EXTI_SINGLE_RE.match(name)
            if not m:
                continue
            mask = 1 << int(m.group(1))
        mask &= EXTI_LINE_MASK
        if mask:
            groups.append((name, mask))
    groups.sort(key=lambda g: g[1])

    out = [
        "/* DO NOT EDIT - machine generated by tools/generate/generate.py",
        f" * source: tools/generate/interrupts/interrupts.csv (variant {variant})",
        " * EXTI vector grouping: handler name and the pin bits it covers. */",
        "#pragma once",
        "",
        f"#define CH32_EXTI_GROUP_COUNT {len(groups)}",
        "",
        "/* X(handler, mask, irqn) for every EXTI vector this variant has. */",
        "#define CH32_EXTI_GROUPS(X) \\",
    ]
    for name, mask in groups:
        irq = "CH32_IRQN_" + name.removesuffix("_IRQHandler")
        out.append(f"    X({name}, 0x{mask:08x}u, {irq}) \\")
    out.append("    /* end */")
    return "\n".join(out) + "\n"


def gen_pins(series: str, rows: list, pads: dict, adc: dict, uarts: dict,
             pwm: dict, handlers: list, remap: dict, commit: str) -> str:
    """Variant pin map for one series (ADR-0010)."""
    parts = sorted(r["part_number"] for r in rows)
    per_part = [pads.get(pn, set()) for pn in parts]
    union = sorted(set().union(*per_part)) if per_part else []
    if not union:
        raise SystemExit(f"{series}: no GPIO pads resolved from device-data")
    common = sorted(set.intersection(*[set(s) for s in per_part]))

    unusable = []
    for eid, spec in UNUSABLE_PADS.items():
        if series in spec["series"]:
            unusable.append((eid, spec))

    def num(port: str, bit: int) -> int:
        return (PORTS.index(port) << 5) | bit

    hi = max(num(p, b) for p, b in union)
    width = max(len(pad_name(p, b)) for p, b in union)

    out = [
        "/* DO NOT EDIT - machine generated by tools/generate/generate.py",
        f" * source: ch32-device-data tables @ git {commit}",
        " *",
        f" * {series} pin map: the union of all {len(parts)} part numbers in the",
        " * series, so the same header serves the ANY menu entry and every SKU.",
        " *",
        " * Pin numbers are port-encoded (ADR-0010): pin = (port << 5) | bit.",
        " * The valid set is SPARSE. NUM_DIGITAL_PINS is one past the highest pin",
        " * number, NOT a pad count, and 0..NUM_DIGITAL_PINS-1 is not a usable",
        " * loop range - test with digitalPinIsValid(pin).",
        " *",
        " * A pad missing from a given package is left unbonded: writing it only",
        " * touches a register bit with nothing attached, which is harmless.",
    ]
    for eid, spec in unusable:
        out.append(" *")
        for line in textwrap.wrap(f"Exception, errata {eid}: {spec['note']}", 72):
            out.append(f" * {line}")
    out += [" */", "#pragma once", "", '#include "ch32_pins.h"', ""]

    out.append(f"#define CH32_VARIANT_{series} 1")
    out.append("")

    out.append(f"/* ---- GPIO pads: {len(union)} in the series, "
               f"{len(common)} of them on every part ---- */")
    for port, bit in union:
        out.append(f"#define {pad_name(port, bit):<{width}} "
                   f"CH32_PIN({PORTS.index(port)}, {bit:2d})")
    out.append("")

    def mask_block(name: str, pads_set, comment: str):
        out.append(comment)
        for port in PORTS:
            mask = 0
            for p, b in pads_set:
                if p == port:
                    mask |= 1 << b
            absent = "" if mask else "   /* port absent */"
            out.append(f"#define CH32_{name}_{port} 0x{mask:08x}u{absent}")
        out.append(f"#define CH32_{name}(port) ( \\")
        for i, port in enumerate(PORTS):
            out.append(f"    (port) == {i} ? CH32_{name}_{port} : \\")
        out.append("    0u)")
        out.append("")

    mask_block("PORT_MASK", union,
               "/* Bit n set = P<port>n is bonded out on at least one part in the "
               "series. */")
    mask_block("PORT_COMMON_MASK", common,
               "/* Bit n set = P<port>n is bonded out on EVERY part in the series,\n"
               " * i.e. the pins a sketch built for the ANY menu entry can rely on. */")

    if unusable:
        pins = []
        for eid, spec in unusable:
            for port, bit in spec["pads"]:
                pins.append(f"CH32_PIN({PORTS.index(port)}, {bit})"
                            f" /* {pad_name(port, bit)}, {eid} */")
        out.append("/* Register bits that must never be driven; see the errata note above. */")
        out.append(f"#define CH32_UNUSABLE_PIN_COUNT {len(pins)}")
        out.append("#define CH32_UNUSABLE_PINS { \\")
        for s in pins:
            out.append(f"    {s}, \\")
        out.append("    }")
        out.append("")

    out.append(f"#define NUM_DIGITAL_PINS {hi + 1}   "
               f"/* highest pin number + 1, not a pad count */")
    out.append(f"#define PINS_COUNT       NUM_DIGITAL_PINS")
    out.append(f"#define CH32_GPIO_COUNT  {len(union)}   /* actual pads in the series */")
    out.append("")

    # --- analog ---
    channels: dict[int, tuple] = {}
    for pn in parts:
        for ch, padkey in adc.get(pn, {}).items():
            prev = channels.setdefault(ch, padkey)
            if prev != padkey:
                raise SystemExit(f"{series}: ADC1 channel {ch} maps to both "
                                 f"{prev} and {padkey} within the series")
    if channels:
        top = max(channels) + 1
        gaps = [c for c in range(top) if c not in channels]
        out.append(f"/* ---- ADC1 analog inputs ({len(channels)} channels) ---- */")
        if gaps:
            out.append(f"/* Channels {gaps} are not bonded out in this series. */")
        note = "   /* highest channel + 1; see the gaps above */" if gaps else ""
        out.append(f"#define NUM_ANALOG_INPUTS {top}{note}")
        for ch in sorted(channels):
            port, bit = channels[ch]
            out.append(f"#define A{ch:<3} {pad_name(port, bit)}")
        out.append("#define CH32_PIN_TO_ADC_CHANNEL(p) ( \\")
        for ch in sorted(channels):
            port, bit = channels[ch]
            out.append(f"    (p) == {pad_name(port, bit)} ? {ch} : \\")
        out.append("    NOT_AN_ANALOG_PIN)")
        out.append("#define CH32_ADC_CHANNEL_TO_PIN(c) ( \\")
        for ch in sorted(channels):
            port, bit = channels[ch]
            out.append(f"    (c) == {ch} ? {pad_name(port, bit)} : \\")
        out.append("    NOT_A_PIN)")
        out.append("")

    # --- default USART pins ---
    # Only emit an entry when every part in the series that has this USART puts
    # it on the same pads; otherwise a sketch built for ANY would target a pin
    # that moves between packages.
    agreed: dict = {}
    for index in sorted({i for pn in parts for i in uarts.get(pn, {})}):
        seen = [uarts[pn][index] for pn in parts
                if index in uarts.get(pn, {}) and
                {"TX", "RX"} <= set(uarts[pn][index])]
        if seen and all(v == seen[0] for v in seen) and len(seen) == len(parts):
            agreed[index] = seen[0]
    # The core drives serial from interrupts, so a USART is only usable when the
    # startup variant actually has a handler slot for it. The handler is named
    # USARTn_IRQHandler on some families and UARTn_IRQHandler on others, so take
    # the name from the vector table instead of guessing.
    handler_of = {}
    for name in handlers:
        if not name:
            continue
        m = re.match(r"^U(?:S)?ART(\d+)_IRQHandler$", name)
        if m:
            handler_of.setdefault(int(m.group(1)), name)
    chosen = choose_uarts(series, parts, uarts, handler_of, remap)
    if chosen:
        out.append("/* ---- USART pins (device-data; one route per USART, chosen for")
        out.append(" *      the whole series - see choose_uarts in generate.py) ---- */")
        for index, (coverage, route, (tx, rx)) in sorted(chosen.items()):
            where = ("on every part" if coverage == len(parts)
                     else f"on {coverage} of {len(parts)} parts")
            out.append(f"/* USART{index}: route {route}, {where} */")
            out.append(f"#define CH32_SERIAL{index}_TX {pad_name(*tx)}")
            out.append(f"#define CH32_SERIAL{index}_RX {pad_name(*rx)}")
            name = handler_of[index]
            out.append(f"#define CH32_SERIAL{index}_HANDLER {name}")
            out.append(f"#define CH32_SERIAL{index}_IRQ "
                       f"CH32_IRQN_{name.removesuffix('_IRQHandler')}")
            value = route_remap_value(route)
            bits = remap.get((series, index))
            if value is None:
                out.append(f"/* NOTE: route {route} is a per-pin alternate-function")
                out.append(" * selector, not an AFIO remap. The core does not program it")
                out.append(" * yet, so this port needs verifying (docs/todo.ja.md). */")
            elif value and bits:
                mask, val = remap_mask_value(bits, value)
                out.append(f"#define CH32_SERIAL{index}_REMAP_MASK 0x{mask:08x}u")
                out.append(f"#define CH32_SERIAL{index}_REMAP_VAL  0x{val:08x}u")
            elif value:
                out.append(f"/* NOTE: route {route} needs an AFIO remap but device-data")
                out.append(f" * has no PCFR1 field for USART{index} in this series. */")
        # Serial points at the USART that reaches the most part numbers.
        def rank(i):
            coverage, route, _pads = chosen[i]
            value = route_remap_value(route)
            programmable = value == 0 or (value is not None and (series, i) in remap)
            return (programmable, coverage, -i)
        default = max(sorted(chosen), key=rank)
        # A board can wire a different USART than the series-wide choice: the
        # generator optimises for the ANY entry (pins present on every part),
        # while a real board only has to work for itself. Overridable with
        # -DCH32_SERIAL_DEFAULT=<n>, which is what tests/hardware/uart_scan.py
        # reports.
        out.append("#ifndef CH32_SERIAL_DEFAULT")
        out.append(f"#define CH32_SERIAL_DEFAULT {default}")
        out.append("#endif")
        out.append("")

    # --- PWM ---
    # Only pads every part agrees on: a sketch built for ANY must not have
    # analogWrite() land on a different timer depending on the package.
    pwm_pads: dict = {}
    conflicting = set()
    for pn in parts:
        for padkey, tc in pwm.get(pn, {}).items():
            prev = pwm_pads.setdefault(padkey, tc)
            if prev != tc:
                conflicting.add(padkey)
    for padkey in conflicting:
        pwm_pads.pop(padkey, None)
    pwm_pads = {k: v for k, v in pwm_pads.items() if k in set(union)}
    if pwm_pads:
        ordered = sorted(pwm_pads.items(), key=lambda kv: (kv[1], kv[0]))
        out.append(f"/* ---- PWM: {len(ordered)} pads on TIM1/TIM2/TIM3, "
                   "default route ---- */")
        out.append(f"#define CH32_PWM_PIN_COUNT {len(ordered)}")
        for name, index in (("TIMER", 0), ("CHANNEL", 1)):
            out.append(f"#define CH32_PWM_PIN_TO_{name}(p) ( \\")
            for padkey, tc in ordered:
                out.append(f"    (p) == {pad_name(*padkey)} ? {tc[index]} : \\")
            out.append("    0)")
        out.append("")

    # --- LED_BUILTIN ---
    led = common[0] if common else union[0]
    out.append("/* Generic boards have no on-board LED. This placeholder only exists so")
    out.append(" * that the stock examples compile; it is the lowest-numbered pad present")
    out.append(" * on every part in the series. Override it per board or on the command")
    out.append(" * line: --build-property build.extra_flags=-DLED_BUILTIN=PC13 */")
    out.append("#ifndef LED_BUILTIN")
    out.append(f"#define LED_BUILTIN {pad_name(*led)}")
    out.append("#endif")
    return "\n".join(out) + "\n"


def gen_board(series: str, rows: list, probe_rs: set, commit: str):
    """One board per series. Returns (boards.txt block, {ld name: content})."""
    cfg = SERIES_CONFIG[series]
    fam = FAMILY[cfg["family"]]
    rows = sorted(rows, key=lambda r: (int(r["flash_bytes"]), int(r["sram_bytes"]),
                                       r["part_number"]))
    board = series
    ordered = [r["part_number"] for r in rows]
    flashable = probe_rs_chip("ANY", series, ordered, probe_rs) is not None
    suffix = "" if flashable else " [compile only]"

    lines = [f"{board}.name=Generic {series}{suffix}"]
    lines.append(f"{board}.build.board={board}")
    lines.append(f"{board}.build.core=arduino")
    lines.append(f"{board}.build.variant={board}")
    lines.append(f"{board}.build.march={fam['march']}")
    lines.append(f"{board}.build.mabi={fam['mabi']}")
    lines.append(f"{board}.build.f_cpu={fam['f_cpu']}")
    lines.append(f"{board}.build.series={series}")
    lines.append(f"{board}.build.startup_defines={fam['defines']}")
    lines.append(f"{board}.build.vectors=vectors_{cfg['vectors']}.inc")
    lines.append(f"{board}.build.core_defines={fam['core_defines']} "
                 f"-DCH32_IRQNS=irqn_{cfg['vectors']}.h "
                 f"-DCH32_EXTIS=exti_{cfg['vectors']}.h")
    lines.append("")

    ld_files = {}

    def ld_for(flash: int, sram: int) -> str:
        name = f"{series.lower()}_{kb(flash)}k_{kb(sram)}k.ld"
        if name not in ld_files:
            ld_files[name] = (
                ld_header(commit)
                + f"/* FLASH {kb(flash)}K, SRAM {kb(sram)}K */\n"
                + "ENTRY( _start )\n\nMEMORY\n{\n"
                + f"    FLASH (rx) : ORIGIN = 0x00000000, LENGTH = {flash}\n"
                + f"    RAM (xrw)  : ORIGIN = 0x20000000, LENGTH = {sram}\n"
                + "}\n\nINCLUDE sections.ld\n")
        return name

    # "Any" entry first, so it is the menu default. It declares the smallest
    # flash and RAM in the series: a binary built for it fits every part, and
    # the stack (top of RAM) always lands inside real memory.
    min_flash = min(int(r["flash_bytes"]) for r in rows)
    min_sram = min(int(r["sram_bytes"]) for r in rows)
    entries = [("ANY", f"Any {series} ({kb(min_flash)}K/{kb(min_sram)}K)",
                min_flash, min_sram)]
    for r in rows:
        flash, sram = int(r["flash_bytes"]), int(r["sram_bytes"])
        entries.append((r["part_number"],
                        f"{r['part_number']} ({r['package']}, {kb(flash)}K/{kb(sram)}K)",
                        flash, sram))

    # rows is sorted smallest flash first, so the ANY fallback below lands on
    # the smallest part probe-rs knows - which is what ANY declares.
    for pn, label, flash, sram in entries:
        pfx = f"{board}.menu.pnum.{pn}"
        lines.append(f"{pfx}={label}")
        lines.append(f"{pfx}.build.board={pn if pn != 'ANY' else series}")
        lines.append(f"{pfx}.build.ldscript={ld_for(flash, sram)}")
        lines.append(f"{pfx}.upload.maximum_size={flash}")
        lines.append(f"{pfx}.upload.maximum_data_size={sram}")
        chip = probe_rs_chip(pn, series, ordered, probe_rs)
        if chip:
            lines.append(f"{pfx}.build.probe_rs_chip={chip}")
        lines.append("")

    return "\n".join(lines), ld_files


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--tables", required=True, type=pathlib.Path)
    ap.add_argument("--platform", required=True, type=pathlib.Path)
    ap.add_argument("--check", action="store_true",
                    help="verify committed files match regeneration; do not write")
    args = ap.parse_args()

    with open(args.tables / "products.csv", newline="", encoding="utf-8") as f:
        products = list(csv.DictReader(f))
    commit = source_commit(args.tables)

    interrupts, vector_forms = load_interrupts()
    pads, adc, unresolved = load_pin_tables(args.tables)
    uarts = load_uart_pins(args.tables)
    probe_rs = load_probe_rs_targets()
    remap = load_remap_fields(args.tables)
    pwm = load_pwm_pins(args.tables)
    errata_ids = load_errata_ids(args.tables)
    stale = sorted(set(UNUSABLE_PADS) - errata_ids)
    if stale:
        print(f"ERROR: UNUSABLE_PADS references errata ids that no longer exist "
              f"in ch32-device-data: {stale}", file=sys.stderr)
        return 1

    by_board = {}
    for r in products:
        board = SKU_BOARD_OVERRIDE.get(r["part_number"], r["series"])
        if board in SERIES_CONFIG:
            by_board.setdefault(board, []).append(r)

    missing = [s for s in SERIES_CONFIG if s not in by_board]
    if missing:
        print(f"ERROR: no products for series {missing}", file=sys.stderr)
        return 1

    outputs = {}
    boards_blocks = []
    used_variants = set()
    for series in SERIES_CONFIG:
        rows = by_board[series]
        block, ld_files = gen_board(series, rows, probe_rs, commit)
        boards_blocks.append(block)
        used_variants.add(SERIES_CONFIG[series]["vectors"])
        for name, content in ld_files.items():
            outputs[args.platform / "variants" / series / name] = content
        outputs[args.platform / "variants" / series / "pins_arduino.h"] = \
            gen_pins(series, rows, pads, adc, uarts, pwm,
                     interrupts[SERIES_CONFIG[series]['vectors']], remap, commit)

    generated_parts = {r["part_number"] for rows in by_board.values() for r in rows}
    blocked = sorted(p for p in unresolved if p[0] in generated_parts)
    if blocked:
        print("ERROR: pads with no port assignment in a generated series "
              "(add to NON_PORT_PADS if they are not GPIO port bits): "
              f"{blocked}", file=sys.stderr)
        return 1

    outputs[args.platform / "boards.txt"] = (
        gen_header(commit) + "\n" + MENU_HEADER + "\n" + "\n".join(boards_blocks))

    for variant in sorted(used_variants):
        if variant not in interrupts:
            print(f"ERROR: no interrupt table for variant {variant} "
                  f"(rebuild with tools/generate/import_vectors.py)", file=sys.stderr)
            return 1
        outputs[args.platform / "cores" / "arduino" / f"vectors_{variant}.inc"] = \
            gen_vectors(variant, interrupts[variant],
                        vector_forms[variant], commit)
        outputs[args.platform / "cores" / "arduino" / f"irqn_{variant}.h"] = \
            gen_irqns(variant, interrupts[variant])
        outputs[args.platform / "cores" / "arduino" / f"exti_{variant}.h"] = \
            gen_exti(variant, interrupts[variant])

    drift = 0
    for path, content in outputs.items():
        if args.check:
            on_disk = path.read_text(encoding="utf-8") if path.exists() else None
            if on_disk != content:
                print(f"DRIFT: {path}")
                drift = 1
            else:
                print(f"ok:    {path}")
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content, encoding="utf-8")
            print(f"wrote: {path}")
    return drift


if __name__ == "__main__":
    sys.exit(main())
