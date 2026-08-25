#!/usr/bin/env python3
"""W-4 prototype generator: boards.txt and linker scripts from ch32-device-data tables.

Reads the normalized CSV tables (products.csv) and writes, per configured family:
  - boards.txt            (board entry + menu.pnum entry per part number)
  - variants/<VARIANT>/<ld>  (one MEMORY script per unique flash/sram combination)

Design rules (see docs/research/board-variants-and-menus.ja.md):
  - one board per mirror family, menu.pnum lists every part number
  - deterministic ordering: canonical series order, then part number
  - generated files carry a DO-NOT-EDIT header; the source repo commit is
    recorded once, in vendor/ch32-device-data.lock.toml, so that an upstream
    bump touches one file rather than all of them
  - no timestamps in output so regeneration is idempotent (CI check mode)

Usage:
  generate.py --tables <ch32-device-data>/tables --platform <platform dir> [--check]
"""
import argparse
import csv
import difflib
import hashlib
import pathlib
import re
import textwrap
import subprocess
import sys

# Per-family generation config. What is left here is either a decision of ours
# or a fact no table holds yet; everything ch32-device-data can answer is read
# from it instead (load_family_facts), so there is no second copy to drift.
#
#   from the tables   CH32_GPIO_PORT_WIDTH, CH32_HSI_HZ, CH32_HPRE_LINEAR
#   checked against   flash_latency, SERIES_CONFIG's vectors variant
#     the tables
#   ours              march/mabi (which optional extensions to enable: the
#                     tables call CH32V407 RV32IMACB+Zve64x_zvbb and we build
#                     rv32imac), f_cpu (HSI direct today)
#   not in any table  systick64, adc_bits, i2c_has_rtr, and the CSR init values
#                     below - the CSR ones come from the EVT startup assembly
#                     and are re-verified every PR by tests/startup/
#
# Values come from verified research:
# march/mabi and startup CSR defines: docs/research/startup-files.ja.md (R-01),
# experiments 0001/0002. Only families proven by the equivalence harness are listed.
# Startup/ISA parameters shared by every series in an EVT family.
# Values come from the equivalence harness table in tests/startup/startup_equivalence.py.
# CH32_HPRE_LINEAR is which of the two AHB-prescaler encodings the family uses,
# read off its own EVT header (RCC_HPRE_DIV2 is 0x10 on one and 0x80 on the
# other); the two tables are written out in cores/arduino/wiring_time.c.
    # CH32V407 and CH32X315 carry flash_latency=0 because neither family has a
    # wait-state field: EVT never writes ACTLR on CH32V407, and CH32X315's
    # ACTLR holds FLASH_ACTLR_SCK_CFG, a flash-clock divider. Both used to be
    # written with a 1 of no traceable origin; check_family_facts rejects that
    # now. Raising CH32X315 past its default needs the divider, which is a
    # separate mechanism (docs/todo.ja.md).
    # 96 MHz rather than the 144 the PLL can reach: ADCPRE divides by at most
    # 8, and f_ADC on these families is 14 MHz, so 144 would leave the ADC at
    # 18 MHz - out of spec with no way to fix it. 96/8 = 12 MHz is inside it,
    # and USB still gets its 48 MHz (PLLCLK/2). Defaults stay in spec.
FAMILY = {
    "CH32V003": dict(march="rv32ec_zicsr", mabi="ilp32e", f_cpu="24000000L",
                     defines="-DCH32_MSTATUS_INIT=0x1880 -DCH32_INTSYSCR_INIT=0x3 -DCH32_HIGHCODE",
                     systick64=0, flash_latency=0, adc_bits=10, i2c_has_rtr=0),
    "CH32V006": dict(march="rv32emc_zicsr", mabi="ilp32e", f_cpu="24000000L",
                     defines="-DCH32_MSTATUS_INIT=0x1880 -DCH32_INTSYSCR_INIT=0x3",
                     systick64=0, flash_latency=1, adc_bits=12, i2c_has_rtr=0),
    "CH32V205": dict(march="rv32imc_zicsr", mabi="ilp32", f_cpu="8000000L",
                     defines="-DCH32_MSTATUS_INIT=0x88 -DCH32_INTSYSCR_INIT=0x7 "
                             "-DCH32_CORECFGR=0x21 -DCH32_CSR_BC1=0x1",
                     systick64=0, flash_latency=0, adc_bits=12, i2c_has_rtr=1),
    "CH32V20x": dict(march="rv32imac_zicsr", mabi="ilp32", f_cpu="96000000L",
                     defines="-DCH32_MSTATUS_INIT=0x88 -DCH32_INTSYSCR_INIT=0x3 "
                             "-DCH32_CORECFGR=0x1f",
                     systick64=1, flash_latency=0, adc_bits=12, i2c_has_rtr=1),
    "CH32V307": dict(march="rv32imafc_zicsr", mabi="ilp32f", f_cpu="96000000L",
                     defines="-DCH32_MSTATUS_INIT=0x6088 -DCH32_INTSYSCR_INIT=0x0b "
                             "-DCH32_CORECFGR=0x1f",
                     systick64=1, flash_latency=0, adc_bits=12, i2c_has_rtr=1),
    "CH32V407": dict(march="rv32imac_zicsr", mabi="ilp32", f_cpu="20000000L",
                     defines="-DCH32_MSTATUS_INIT=0x688 -DCH32_INTSYSCR_INIT=0x07 "
                             "-DCH32_CORECFGR=0x21 -DCH32_CSR_BC1=0x01 -DCH32_CSR805_CLR=0x100",
                     systick64=0, flash_latency=0, adc_bits=12, i2c_has_rtr=1),
    "CH32X035": dict(march="rv32imac_zicsr", mabi="ilp32", f_cpu="48000000L",
                     defines="-DCH32_MSTATUS_INIT=0x88 -DCH32_INTSYSCR_INIT=0x3 "
                             "-DCH32_CORECFGR=0x1f",
                     systick64=1, flash_latency=2, adc_bits=12, i2c_has_rtr=0),
    "CH32X315": dict(march="rv32imafc_zicsr", mabi="ilp32f", f_cpu="20000000L",
                     defines="-DCH32_MSTATUS_INIT=0x6088 -DCH32_INTSYSCR_INIT=0x07 "
                             "-DCH32_CORECFGR=0x123703E1 -DCH32_CSR_BC1=0x01",
                     systick64=0, flash_latency=0, adc_bits=12, i2c_has_rtr=0),
    # CH32V103's table is a jump table and its startup never writes csr 0x804.
    "CH32V103": dict(march="rv32imac_zicsr", mabi="ilp32", f_cpu="72000000L",
                     defines="-DCH32_MSTATUS_INIT=0x88 -DCH32_MTVEC_MODE=1",
                     systick64=0, flash_latency=0, adc_bits=12, i2c_has_rtr=1),
    "CH32L103": dict(march="rv32imac_zicsr", mabi="ilp32", f_cpu="8000000L",
                     defines="-DCH32_MSTATUS_INIT=0x88 -DCH32_INTSYSCR_INIT=0x3 "
                             "-DCH32_CORECFGR=0x1f",
                     systick64=1, flash_latency=0, adc_bits=12, i2c_has_rtr=1),
    "CH32M030": dict(march="rv32imc_zicsr", mabi="ilp32", f_cpu="8000000L",
                     defines="-DCH32_MSTATUS_INIT=0x88 -DCH32_INTSYSCR_INIT=0x3 "
                             "-DCH32_CORECFGR=0x21 -DCH32_CSR_BC1=0x1",
                     systick64=0, flash_latency=0, adc_bits=12, i2c_has_rtr=0),
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
    # D8C, not D8: ch32v30x.h says so ("CH32V307x-CH32V305x-CH32V317x") and
    # evt_variants.csv carries the same mapping. CH32V305 is the USB-HS part,
    # and the D8 table leaves USBHS_IRQHandler and USBHSWakeup_IRQHandler
    # reserved, so building it as D8 left its headline peripheral with no
    # vector at all.
    "CH32V305": dict(family="CH32V307", vectors="v307_d8c"),
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

MENU_HEADER = "menu.pnum=Part Number\nmenu.printf=printf() float support\n"

# printf float support as a menu entry. PROPOSED, NOT APPROVED - see
# docs/approval-status.ja.md A-1. ADR-0004 proposes this shape but is still
# Proposed. The measurement behind it: on CH32X035 a printf sketch is 48,492
# bytes with the full newlib formatter and 7,064 with nano, and CH32V003 has
# only 16 KB of flash. Both entries are emitted for every board because the
# choice is per-sketch, not per-part.
PRINTF_MENU = (
    ("none", "No float (smaller)", ""),
    ("float", "%f supported (+~19 KB)", "-Wl,-u,_printf_float"),
)


DEVICE_DATA_URL = "https://github.com/ch32-riscv-ug/ch32-device-data.git"
LOCK_REL = "vendor/ch32-device-data.lock.toml"

# Every table the generator reads. read_table() is the only door in, so the
# lock can hash exactly the inputs and nothing else: an upstream commit that
# leaves all of them alone cannot change a single generated byte.
_READ_TABLES: set = set()


def read_table(tables: pathlib.Path, name: str, require: tuple = ()) -> list:
    """One ch32-device-data CSV, as a list of dicts.

    `require` names columns this caller reads. Upstream adds columns over time
    (operating_conditions.csv grew `typ` for the oscillators), so a pin that
    predates one produces a KeyError deep in a loader; checking here says which
    table is too old instead.
    """
    _READ_TABLES.add(name)
    try:
        with open(tables / name, newline="", encoding="utf-8") as f:
            rows = list(csv.DictReader(f))
    except FileNotFoundError:
        raise SystemExit(f"ERROR: {name} is not in these tables. The pinned "
                         f"ch32-device-data commit predates it "
                         f"({LOCK_REL}).") from None
    missing = [c for c in require if rows and c not in rows[0]]
    if missing:
        raise SystemExit(f"ERROR: {name} has no {', '.join(missing)} column. "
                         f"The pinned ch32-device-data commit predates it "
                         f"({LOCK_REL}).")
    return rows


def source_commit(tables_dir: pathlib.Path) -> str:
    try:
        out = subprocess.run(["git", "-C", str(tables_dir), "rev-parse", "HEAD"],
                             capture_output=True, text=True, check=True)
        return out.stdout.strip()
    except Exception:
        return "unknown"


def source_is_dirty(tables_dir: pathlib.Path) -> bool:
    """Whether the tables differ from the commit that will be recorded."""
    try:
        out = subprocess.run(["git", "-C", str(tables_dir), "status",
                              "--porcelain", "--", "."],
                             capture_output=True, text=True, check=True)
        return bool(out.stdout.strip())
    except Exception:
        return False


def gen_lock(tables: pathlib.Path, commit: str) -> str:
    """The ch32-device-data pin: the commit, plus a hash per table read.

    The commit used to sit in the header of all 55 generated files, which made
    every upstream bump a 55-file diff even when not one generated byte moved.
    It lives here once instead. The hashes then say something the commit
    cannot - whether the tables *this generator reads* actually changed - so a
    bump that only touched, say, the clock tables is provably a no-op offline.

    Call this after generation: _READ_TABLES is only complete by then.
    """
    lines = [
        "# ch32-device-data pin. DO NOT EDIT - machine generated by",
        "# tools/generate/generate.py; run the generator to move the pin.",
        "#",
        "# Unlike the other vendor locks, nothing is copied into this tree: the",
        "# tables stay upstream and only their identity is recorded here.",
        "# tools/index/fetch_tools.py checks out `commit` for the test harnesses.",
        "",
        "[[source]]",
        'id = "ch32-device-data"',
        f'url = "{DEVICE_DATA_URL}"',
        f'commit = "{commit}"',
        "",
        "# SHA-256 of every table the generator reads, relative to tables/.",
        "# A commit that leaves all of these unchanged cannot change any",
        "# generated file, which is the whole reason they are listed.",
        "files = [",
    ]
    for name in sorted(_READ_TABLES):
        digest = hashlib.sha256((tables / name).read_bytes()).hexdigest()
        lines.append(f'  {{ path = "{name}", sha256 = "{digest}" }},')
    lines.append("]")
    return "\n".join(lines) + "\n"


# What the generated files say about where they came from. Deliberately no
# commit id: it would put the same fact in 55 files and turn every upstream
# bump into a 55-file diff. The id lives in LOCK_REL alone. These headers ship
# to users inside the release archive, which does not carry vendor/, so they
# name the upstream repository rather than only pointing at a file.
SOURCE_LINE = ("source: ch32-riscv-ug/ch32-device-data tables, "
               f"pinned in {LOCK_REL}")


def gen_header() -> str:
    return ("# DO NOT EDIT - machine generated by tools/generate/generate.py\n"
            f"# {SOURCE_LINE}\n"
            "# Regenerate: generate.py --tables <ch32-device-data>/tables "
            "--platform <platform dir>\n")


def ld_header() -> str:
    return ("/* DO NOT EDIT - machine generated by tools/generate/generate.py\n"
            f" * {SOURCE_LINE} */\n")


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


def gen_vectors(variant: str, entries: list, form: str) -> str:
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
def load_wide_timers(tables: pathlib.Path) -> dict:
    """family -> {timer numbers whose counter is wider than 16 bits}.

    On such a timer CNT, ATRLR and CHnCVR are one 32-bit register, and a
    16-bit store is replicated into both halves - see ch32_registers.h. This
    was a hand-written table (WIDE_TIMERS) until timers.csv arrived upstream;
    the hand-written version had every emitted value right but one basis
    wrong (it called the plain CH32V20x TIM4 wide on the strength of the EVT
    header's union, where the reference manual says 16 bits - the wide one is
    the V205 die that shares that header).
    """
    wide: dict = {}
    for r in read_table(tables, "timers.csv"):
        if int(r["counter_width_bits"] or 16) > 16:
            number = int(r["timer"].removeprefix("TIM"))
            # `condition` narrows some rows to a die variant (TIM5 is 32-bit
            # only on the V20x D8/D8W dies). Ignored on purpose: treating the
            # timer as wide everywhere in the family is the safe direction,
            # because a 32-bit store to a 16-bit timer is harmless (measured
            # on CH32V203C8T6) while the reverse silenced tone() on CH32L103.
            wide.setdefault(r["family"], set()).add(number)
    return wide


UNUSABLE_PADS = {
    "x035-pc10-pc17-bonded": {
        "series": ("CH32X033", "CH32X035"),
        "pads": (("C", 10), ("C", 11)),
        # Upstream refined this while the pads were still absent from the
        # tables: the pairs PC10/PC17 and PC11/PC16 each share one physical
        # lead, so the two halves of a pair must never both be outputs.
        # Marking the PC10/PC11 side unusable is the conservative half of
        # that rule, kept even now that they are generated as real pads.
        "note": ("PC10/PC17 and PC11/PC16 are internally bonded pairs "
                 "sharing one lead; never drive both halves of a pair. The "
                 "core marks PC10/PC11 as the never-driven side."),
    },
}


def load_pin_tables(tables: pathlib.Path):
    """(pads, adc) keyed by part number.

    pads: part -> sorted [(port, bit)]
    adc:  part -> {channel: (port, bit)} for ADC1 only (see below)
    """
    functions = read_table(tables, "pin_functions.csv")
    pinrows = read_table(tables, "pins.csv")

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
    return {r["id"] for r in read_table(tables, "errata.csv")}


# ------------------------------------------------------- facts from the tables
def _family_series(tables: pathlib.Path) -> dict:
    """family -> its series names. families.csv is the only place that says so."""
    return {r["family"]: r["series"].split(";")
            for r in read_table(tables, "families.csv", ("family", "series"))}


def load_family_facts(tables: pathlib.Path, pads: dict, products: list) -> dict:
    """family -> the values FAMILY used to spell out.

    One value per family, and the table has to agree with itself: a family is
    one silicon, so if two of its series disagree about the HSI that is a data
    bug and guessing which one is right would hide it.
    """
    series_of = {f: set(s) for f, s in _family_series(tables).items()}
    fam_of_series = {s: f for f, ss in series_of.items() for s in ss}
    fam_of_part = {r["part_number"]: fam_of_series.get(r["series"])
                   for r in products}

    def one(family: str, what: str, values: set):
        if len(values) != 1:
            raise SystemExit(f"ERROR: {family}: ch32-device-data gives "
                             f"{len(values)} answers for {what}: {sorted(values)}")
        return values.pop()

    # HSI: operating_conditions carries it as a typical value, because the
    # datasheets specify the oscillator as "typ plus ACC_HSI percent" and have
    # no min/max for it. The CH32V00x rows are qualified by HSI_LP, whose 0
    # branch is the normal-speed one; the low-power rows are kHz and belong to
    # a mode we do not drive.
    hsi: dict = {}
    for r in read_table(tables, "operating_conditions.csv",
                        ("series", "symbol", "condition", "typ", "unit")):
        if r["symbol"] != "F_HSI" or r["unit"] != "MHz" or not r["typ"]:
            continue
        if r["condition"] not in ("", "HSI_LP = 0"):
            continue
        for s in r["series"].split(";"):
            f = fam_of_series.get(s)
            if f:
                hsi.setdefault(f, set()).add(int(float(r["typ"]) * 1e6))

    # LSI: the watchdog's clock. Several datasheets give slightly different
    # typicals for the same family (V203 alone has three rows: its own sheet,
    # an RBT6-qualified one, and the V205 sheet's), and the oscillator is
    # loosely specified anyway (min..max spans a factor of two on some parts),
    # so the *largest* unqualified typical is taken: assuming a fast LSI makes
    # a requested watchdog timeout come out shorter than asked, never longer,
    # and "shorter than asked" is the survivable direction for a watchdog.
    # X033/X035 publish no F_LSI at all (requested upstream); those families
    # simply get no CH32_LSI_HZ and wdtEnable() says so.
    lsi: dict = {}
    for r in read_table(tables, "operating_conditions.csv",
                        ("series", "symbol", "condition", "typ", "unit")):
        if r["symbol"] != "F_LSI" or not r["typ"] or r["condition"]:
            continue
        if r["unit"].lower() != "khz":
            continue
        for s in r["series"].split(";"):
            f = fam_of_series.get(s)
            if f:
                hz = int(float(r["typ"]) * 1e3)
                lsi[f] = max(lsi.get(f, 0), hz)

    # Which families have an independent watchdog at all, and where. Eleven
    # do; CH32M030 does not (its header has no IWDG block), and a wdtEnable()
    # that key-writes into a hole while returning true would be a lie there.
    iwdg: dict = {}
    for r in read_table(tables, "memory_map.csv",
                        ("family", "region", "base_address")):
        if r["region"] == "IWDG" and r["base_address"]:
            iwdg.setdefault(r["family"], set()).add(int(r["base_address"], 16))

    # AHB prescaler encoding. The two schemes agree on /1 and differ on /2,
    # which is the cheapest question that separates them: 0x10 counts (/2 is
    # field 1) and 0x80 is a power-of-two field. Reading /2 wrong runs the part
    # at twice the clock every timing calculation assumes.
    hpre: dict = {}
    for r in read_table(tables, "clock_prescalers.csv",
                        ("family", "field", "divider", "value")):
        if r["field"] == "HPRE" and r["divider"] == "2":
            hpre.setdefault(r["family"], set()).add(int(r["value"]))

    # Flash wait states: the LATENCY field is two bits on most families, three
    # on CH32M030 and four on CH32V205, and absent where the flash needs no
    # wait states (CH32V20x/V307/V407 never write ACTLR, so the field is left
    # alone rather than written with a 0 that may mean something else).
    # CH32X315 and CH32H417 spell the field FLASH_ACTLR_SCK_CFG and it is a
    # flash-clock divider, not a wait count, so it is deliberately not read
    # here - that is a separate mechanism (docs/todo.ja.md).
    latency_mask: dict = {}
    for r in read_table(tables, "clock_symbols.csv",
                        ("family", "symbol", "role", "value")):
        if r["symbol"] == "FLASH_ACTLR_LATENCY" and r["role"] == "mask":
            latency_mask.setdefault(r["family"], set()).add(int(r["value"]))

    # ADC clock ceiling. Hard-coding one number was wrong in both directions:
    # 14 MHz is the CH32V103/V20x/V30x figure, which left CH32X035 running its
    # ADC at 12 MHz against an 8 MHz limit, and needlessly halved it on
    # CH32L103 (48), CH32V205 (64) and CH32X315 (80).
    #
    # Several families give a different ceiling per supply voltage - CH32V003
    # is 6/12/24 MHz at 2.8/3.2/4.5 V and CH32X035 is 6/8 MHz around 3.2 V - and
    # the core cannot know the board's VDD, so the lowest is taken. A slower
    # conversion is a cost; running the ADC out of spec is a wrong reading.
    adc_hz: dict = {}
    for r in read_table(tables, "operating_conditions.csv",
                        ("series", "symbol", "max", "unit")):
        if r["symbol"] != "f_ADC" or not r["max"] or r["unit"] != "MHz":
            continue
        for s in r["series"].split(";"):
            f = fam_of_series.get(s)
            if f:
                hz = int(float(r["max"]) * 1e6)
                adc_hz[f] = min(adc_hz.get(f, hz), hz)

    # Port width: the widest pad the parts of this family actually have. Taken
    # from the resolved pad set rather than pins.csv so it is the same set the
    # pin map is built from - a pad excluded there must not widen the port.
    width: dict = {}
    for part, got in pads.items():
        f = fam_of_part.get(part)
        if f:
            width[f] = max(width.get(f, 0), max(b for _, b in got) + 1)

    facts = {}
    for family in FAMILY:
        if family not in series_of:
            raise SystemExit(f"ERROR: families.csv has no {family}")
        enc = one(family, "the HPRE /2 encoding", set(hpre.get(family, ())))
        if enc not in (0x10, 0x80):
            raise SystemExit(f"ERROR: {family}: HPRE /2 is {enc:#x}, which is "
                             "neither of the two known encodings")
        masks = set(latency_mask.get(family, ()))
        facts[family] = dict(
            hsi_hz=one(family, "the HSI frequency", set(hsi.get(family, ()))),
            hpre_linear=1 if enc == 0x10 else 0,
            port_width=one(family, "the GPIO port width",
                           {width[family]} if family in width else set()),
            latency_mask=one(family, "the flash LATENCY mask", masks) if masks
            else 0,
            adc_max_hz=adc_hz.get(family, 0),
            lsi_hz=lsi.get(family, 0),
            iwdg_base=one(family, "the IWDG base",
                          set(iwdg[family])) if family in iwdg else 0,
        )
    return facts


def load_die_macros(tables: pathlib.Path) -> tuple:
    """(series -> default macro, part -> macro) for the families that have die
    variants. The macro is what clock_configs.csv's conditions are written
    against, so it is kept whole here rather than reduced to a suffix."""
    part_series = {r["part_number"]: r["series"]
                   for r in read_table(tables, "products.csv")}
    by_part, by_series = {}, {}
    for r in read_table(tables, "evt_variants.csv",
                        ("family", "macro", "part_number", "default")):
        if not re.search(r"_D\d\w*$", r["macro"]):
            continue
        by_part[r["part_number"]] = r["macro"]
        if r["default"] == "yes":
            by_series[part_series.get(r["part_number"])] = r["macro"]
    for part, macro in by_part.items():
        by_series.setdefault(part_series.get(part), macro)
    return by_series, by_part


def load_die_variants(tables: pathlib.Path) -> dict:
    """part number -> startup variant, for the parts that need a different one
    from the rest of their series.

    A series is normally one die, so one board is one vector table. CH32V203 is
    not: eleven of its twelve part numbers are CH32V20x_D6 and CH32V203RBT6 is
    CH32V20x_D8, and the two tables stop agreeing at slot 61, where D6 has
    UART4 and D8 has ETH. Building that one part as D6 gives it the wrong
    interrupt numbers, so its menu entry overrides build.vector_variant.

    Which macro a part number carries is evt_variants.csv's answer, read out of
    the EVT device header; the variant *names* are ours, so only the suffix
    crosses over.
    """
    variant_suffix = re.compile(r"_(D\d\w*)$")
    part_series = {r["part_number"]: r["series"]
                   for r in read_table(tables, "products.csv")}
    suffix_of: dict = {}
    default_of: dict = {}
    for r in read_table(tables, "evt_variants.csv",
                        ("family", "macro", "part_number", "default")):
        m = variant_suffix.search(r["macro"])
        series = part_series.get(r["part_number"])
        if not (m and series):
            continue
        suffix_of[r["part_number"]] = m.group(1).lower()
        if r["default"] == "yes":
            default_of[series] = m.group(1).lower()

    out = {}
    for part, suffix in suffix_of.items():
        series = part_series[part]
        cfg = SERIES_CONFIG.get(series)
        if cfg is None:
            continue
        base, _, own = cfg["vectors"].rpartition("_")
        if not base or suffix == own:
            continue                      # same die as the board it sits on
        out[part] = f"{base}_{suffix}"
    return out


# The compile-time branches EVT's setters carry. Anything not listed makes the
# generator stop rather than guess: picking the wrong branch of a clock setter
# is a part that boots at the wrong speed, or not at all.
COND_DIE = re.compile(r"^(?:ifdef|if defined)\s*\(?\s*(\w+)\s*\)?$")


def _condition_matches(condition: str, die: str | None):
    """(matches, is_else) for one clock_configs row, given the series' die macro."""
    condition = condition.strip()
    if not condition:
        return True, False
    if condition == "else":
        return False, True
    if condition == "if (PLL_Source == HSI)":
        return True, False        # HSI is the source this core uses
    m = COND_DIE.match(condition)
    if m:
        return m.group(1) == die, False
    raise SystemExit(f"ERROR: clock_configs.csv has a condition the generator "
                     f"does not understand: {condition!r}")


def load_clock(tables: pathlib.Path) -> tuple:
    """(configs, symbols, prescalers) in the shapes the resolver below wants."""
    configs = read_table(tables, "clock_configs.csv",
                         ("family", "config", "source", "condition", "domains",
                          "hpre", "pll", "flash_latency", "outside_rcc"))
    symbols: dict = {}
    for r in read_table(tables, "clock_symbols.csv",
                        ("family", "symbol", "role", "address", "value")):
        symbols[(r["family"], r["symbol"])] = r
    return configs, symbols


def resolve_clock(family: str, hz: int, die: str | None, hsi_hz: int,
                  configs: list, symbols: dict) -> dict:
    """What SystemInit has to write to run this family's SYSCLK at hz off HSI.

    Returns the numbers, not the symbol names: the same name is a different
    value on different silicon (CH32V307's RCC_PLLMULL18 is 0x003C0000 and
    RCC_PLLMULL18_EXTEN is 0), so resolving here is the only place the die is
    still known.

    The wait states come from the same row, because they belong to the clock
    rather than to the family: CH32V103 wants 0 at 8 MHz and 2 at 72, and
    getting that wrong is a part that cannot fetch its own code.
    """
    want = [r for r in configs
            if r["family"] == family and r["source"] == "HSI"
            and f"SYSCLK={hz}" in r["domains"].split(";")[0]]
    if not want:
        if hz == hsi_hz:
            # The reset default: no setter configures it because nothing has to
            # be written. CH32V20x and CH32V30x sit here until raised.
            return dict(sysclk=hz, pll_mask=0, pll_value=0, exten_addr=0,
                        exten_bits=0, latency=None, config=None)
        raise SystemExit(f"ERROR: {family}: ch32-device-data lists no HSI clock "
                         f"configuration for SYSCLK={hz}")
    chosen = [r for r in want if _condition_matches(r["condition"], die)[0]]
    if not chosen:
        chosen = [r for r in want if _condition_matches(r["condition"], die)[1]]
    if len(chosen) != 1:
        raise SystemExit(f"ERROR: {family}: {len(chosen)} clock configurations "
                         f"match SYSCLK={hz} for die {die}: "
                         f"{[r['config'] for r in chosen]}")
    row = chosen[0]

    # The value is the OR of the named constants; the mask to clear first is
    # the OR of the fields they sit in, found by longest-prefix against the
    # family's own mask symbols. Clearing a fixed 0x003F0000 would be wrong on
    # CH32V205 (five-bit multiplier), CH32V407 (different position) and
    # CH32V307 (PLL2MUL and PLL3MUL share the register).
    fields = sorted((s for (f, s), r in symbols.items()
                     if f == family and r["role"] == "mask"),
                    key=len, reverse=True)
    value = mask = 0
    for name in filter(None, row["pll"].split(";")):
        sym = symbols.get((family, name))
        if sym is None:
            raise SystemExit(f"ERROR: {family}: clock_symbols.csv does not "
                             f"resolve {name}")
        value |= int(sym["value"])
        owner = next((f for f in fields if name.startswith(f)), None)
        if owner is None:
            raise SystemExit(f"ERROR: {family}: no mask symbol owns {name}")
        mask |= int(symbols[(family, owner)]["value"])

    exten_addr = exten_bits = 0
    for name in row["outside_rcc"].split():
        if "->" in name:
            continue                       # the register, named by its bits below
        sym = symbols.get((family, name))
        if sym is None:
            raise SystemExit(f"ERROR: {family}: clock_symbols.csv does not "
                             f"resolve {name}")
        addr = int(sym["address"], 16)
        if exten_addr and exten_addr != addr:
            raise SystemExit(f"ERROR: {family}: outside_rcc spans two registers")
        exten_addr, exten_bits = addr, exten_bits | int(sym["value"])

    return dict(sysclk=hz, pll_mask=mask, pll_value=value,
                exten_addr=exten_addr, exten_bits=exten_bits,
                latency=int(row["flash_latency"]) if row["flash_latency"]
                else None,
                config=row["config"] if row["pll"] else None)


# EVT's SystemInit starts by putting RCC back to a known state, and that turns
# out to be load-bearing rather than tidy: PLLSRC and PLLMULL are read-only
# while PLLON is set, so a PLL left running by whatever ran before silently
# swallows the new configuration. Measured on CH32V103, where a leftover
# HSE x9 survived a reflash and the core's own PLL write did nothing.
#
# The steps are family-specific (CH32V103 clears CFGR0 with 0xf8ff0000 and
# CH32V20x with 0xf0ff0000; CH32X035 needs three and CH32X315 sixteen), so
# they are generated rather than written out once.
CLOCK_INIT_ACTIONS = ("set", "clear", "write", "poll")


def load_clock_init(tables: pathlib.Path) -> dict:
    """family -> the SystemInit steps, in order."""
    out: dict = {}
    for r in read_table(tables, "clock_init.csv",
                        ("family", "function", "step", "action", "address",
                         "value", "condition")):
        if r["function"] == "SystemInit":
            out.setdefault(r["family"], []).append(r)
    for steps in out.values():
        steps.sort(key=lambda r: int(r["step"]))
    return out


def gen_clock_init(family: str, steps: list, symbols: dict) -> str:
    """The reset sequence for one family, as a macro SystemInit expands."""
    out = [
        "/* DO NOT EDIT - machine generated by tools/generate/generate.py",
        f" * {SOURCE_LINE}",
        " *",
        f" * {family}: what EVT's SystemInit writes before configuring the",
        " * clock. Order matters, and so does doing it at all - PLLSRC and",
        " * PLLMULL cannot be changed while PLLON is set, so a PLL left running",
        " * by a previous program would otherwise keep its configuration.",
        " */",
        "#pragma once",
        "",
        "#define CH32_CLOCK_INIT_RESET() do { \\",
    ]
    skipped = []
    for r in steps:
        action, value = r["action"], r["value"]
        if action not in CLOCK_INIT_ACTIONS:
            # Only CH32V003 has one, an HSI calibration load. The field it
            # writes is not in clock_symbols.csv, so it cannot be emitted;
            # leaving it out matches what the core did before.
            skipped.append(f"step {r['step']} ({action})")
            continue
        addr, v = int(r["address"], 16), int(value)
        reg = f"CH32_REG32({addr:#010x}u)"
        if action == "set":
            out.append(f"    {reg} |= {v:#010x}u; \\")
        elif action == "clear":
            out.append(f"    {reg} &= {v:#010x}u; \\")
        elif action == "write":
            out.append(f"    {reg} = {v:#010x}u; \\")
        else:                                   # poll
            want = r["condition"].removeprefix("!=").strip()
            if want.startswith("0x") or want.isdigit():
                target = int(want, 0)
            else:
                sym = symbols.get((family, want))
                if sym is not None:
                    target = int(sym["value"])
                elif v and not (v & (v - 1)):
                    # A one-bit mask leaves one reading: wait for that bit to
                    # be set. CH32X315 polls "!= RCC_HSIRDY" with mask 0x2 and
                    # clock_symbols.csv does not carry RCC_HSIRDY, so the mask
                    # is the only thing that says which bit - and for a ready
                    # flag the target cannot be anything else.
                    target = v
                    print(f"NOTE: {family}: clock_init.csv polls for {want}, "
                          f"which clock_symbols.csv does not resolve; the "
                          f"one-bit mask {v:#x} fixes the target",
                          file=sys.stderr)
                else:
                    raise SystemExit(f"ERROR: {family}: clock_init.csv polls "
                                     f"for {want} against mask {v:#x}, and "
                                     f"clock_symbols.csv does not resolve it")
            out.append(f"    while (({reg} & {v:#010x}u) != {target:#010x}u) {{}} \\")
    out.append("} while (0)")
    if skipped:
        # index 7 is the closing */, so the note has to go before it
        out.insert(7, f" * NOT emitted: {', '.join(skipped)} - see docs/todo.ja.md.")
        print(f"WARNING: {family}: clock_init step(s) not emitted: "
              f"{', '.join(skipped)}", file=sys.stderr)
    return "\n".join(out) + "\n"


def check_family_facts(tables: pathlib.Path, facts: dict) -> list:
    """Values we still write by hand, against what the tables say. Returns
    complaints rather than raising, so one run reports all of them."""
    bad = []

    # The EVT header picks its register set and its vector table with the same
    # macro, so evt_variants.csv decides which startup variant a series needs.
    # The variant *names* are ours, so this compares rather than derives: the
    # suffix after the last underscore has to be the macro's own suffix.
    # evt_variants.csv holds two kinds of macro: die variants (CH32V20x_D8W)
    # and plain device selectors (CH32V002). Only the first kind picks a
    # startup file, and WCH spells those as a D followed by a digit.
    variant_suffix = re.compile(r"_(D\d\w*)$")
    part_series = {r["part_number"]: r["series"]
                   for r in read_table(tables, "products.csv")}
    by_series: dict = {}
    for r in read_table(tables, "evt_variants.csv",
                        ("family", "macro", "part_number", "default")):
        m = variant_suffix.search(r["macro"])
        s = part_series.get(r["part_number"])
        if m and s:
            by_series.setdefault(s, {}).setdefault(
                (m.group(1).lower(), r["default"] == "yes"), []
            ).append(r["part_number"])
    for series, cfg in sorted(SERIES_CONFIG.items()):
        found = by_series.get(series)
        if not found:
            continue                      # no die variants in this family
        # The header's uncommented macro is what a part gets unless told
        # otherwise, so that is the one a whole-series board has to match.
        default = [k for k in found if k[1]] or list(found)
        suffix = sorted(default)[0][0]
        got = cfg["vectors"].rsplit("_", 1)
        if len(got) != 2 or got[1] != suffix:
            bad.append(f"{series}: vectors={cfg['vectors']!r} but "
                       f"evt_variants.csv says the part numbers are {suffix.upper()}")
        # SKUs on a different die variant are not a problem here: gen_board
        # gives them their own build.vector_variant (load_die_variants).

    # Flash latency at the clock we boot at. Only checkable where EVT ships a
    # setter for exactly that frequency off HSI; most families reach their
    # default by not configuring anything, so there is nothing to compare to.
    # An ADC with no ceiling would be clocked by a guess.
    for family in FAMILY:
        if not facts[family]["adc_max_hz"]:
            bad.append(f"{family}: operating_conditions.csv gives no f_ADC")

    # A wait-state count that does not fit its field would be silently
    # truncated, which is the failure that only shows up at speed.
    for family, fam in FAMILY.items():
        mask = facts[family]["latency_mask"]
        if fam["flash_latency"] & ~mask:
            bad.append(f"{family}: flash_latency={fam['flash_latency']} does not "
                       f"fit FLASH_ACTLR_LATENCY (mask {mask:#x})")

    configs = read_table(tables, "clock_configs.csv")
    for family, fam in FAMILY.items():
        hz = int(fam["f_cpu"].rstrip("L"))
        want = {r["flash_latency"] for r in configs
                if r["family"] == family and r["source"] == "HSI"
                and not r["pll"] and f"SYSCLK={hz}" in r["domains"]
                and r["flash_latency"]}
        if want and want != {str(fam["flash_latency"])}:
            bad.append(f"{family}: flash_latency={fam['flash_latency']} but the "
                       f"{hz // 1000000} MHz HSI configs say {sorted(want)}")
    return bad


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

# I2C signal naming is not normalized either: V003/X033/X035 say bare SCL/SDA,
# M030/V002/V006/V407 say I2C_SCL/I2C_SDA, everyone else says I2C1_SCL. The
# single-instance families all mean instance 1. SMBA is not a role the core
# uses, so it is not matched at all.
I2C_SIGNAL_RE = [
    (re.compile(r"^I2C(\d+)_(SCL|SDA)$"), lambda m: (int(m.group(1)), m.group(2))),
    (re.compile(r"^I2C_(SCL|SDA)$"),      lambda m: (1, m.group(1))),
    (re.compile(r"^(SCL|SDA)$"),          lambda m: (1, m.group(1))),
]
# Only the instances the register map covers. Both sit on APB1; families with
# one I2C simply have no I2C2 pins in device-data, so nothing is emitted.
I2C_BASES = {1: "CH32_I2C1_BASE", 2: "CH32_I2C2_BASE"}
# X033/X035 reach remap-5, and the af-N families (V205/X305/X315) carry only
# alternate-function numbers - listed so the chooser can see them and say so.
I2C_ROUTE_ORDER = ("default", "main", "af-3", "af-7",
                   "remap-1", "remap-2", "remap-3", "remap-4", "remap-5")

# SPI, same three spellings. NSS is deliberately not a role: Arduino drives
# chip select as an ordinary GPIO, so requiring the peripheral's own NSS pad
# would throw away routes that are perfectly usable.
# NSS is matched so that PIN_SPI_SS can be named, but it is deliberately not
# one of the roles a route is chosen by: Arduino drives chip select as an
# ordinary GPIO, and requiring the peripheral's NSS pad would throw away routes
# that are perfectly usable. X315 spells it SCS.
SPI_SIGNAL_RE = [
    (re.compile(r"^SPI(\d+)_(SCK|MISO|MOSI|NSS)$"), lambda m: (int(m.group(1)), m.group(2))),
    (re.compile(r"^SPI(\d+)_SCS$"),                 lambda m: (int(m.group(1)), "NSS")),
    (re.compile(r"^SPI_(SCK|MISO|MOSI|NSS)$"),       lambda m: (1, m.group(1))),
    (re.compile(r"^(SCK|MISO|MOSI|NSS)$"),           lambda m: (1, m.group(1))),
]
SPI_BASES = {1: "CH32_SPI1_BASE", 2: "CH32_SPI2_BASE", 3: "CH32_SPI3_BASE"}

# DAC. One pad per channel, no remap to speak of - the output is wired to a
# fixed pin - so this is only about naming the pad analogWrite() should treat
# as a converter rather than as a PWM pin.
DAC_SIGNAL_RE = [
    (re.compile(r"^DAC(\d+)_OUT$"), lambda m: (int(m.group(1)), "OUT")),
]
DAC_ROUTE_ORDER = ("default", "main")
SPI_ROUTE_ORDER = ("default", "main", "af-4", "af-5",
                   "remap-1", "remap-2", "remap-3", "remap-4",
                   "remap-5", "remap-6")


# AFIO remap. device-data gives the controlling field per series and selector;
# the bit list can be non-contiguous (CH32V003 USART1_REMAP is bits 2 and 21),
# so a value is spread over the listed positions, least significant bit first.
REMAP_SELECTOR_RE = re.compile(r"^afio-(u(?:s)?art|i2c|spi)(\d+)-(?:rm|remap)$")
CH32_AFIO_PCFR1_OFFSET = 0x04


def load_remap_fields(tables: pathlib.Path) -> dict:
    """(series, "usart" | "i2c", index) -> [(register, bit)], LSB first.

    A field is not always one run of bits in one register: on L103/M103 the
    USART1 selector is PCFR1 bit 2 plus PCFR2 bits 19-20, and on V20x/V30x it
    is PCFR1 bit 2 plus PCFR2 bit 26. device-data qualifies every bit with its
    register for exactly that reason, so keep the qualification - dropping it
    and writing PCFR1 alone selects a different route without saying so.
    """
    rows = read_table(tables, "remap_fields.csv")
    out: dict = {}
    for r in rows:
        m = REMAP_SELECTOR_RE.match(r["selector"])
        if not m or r["controller"] != "afio":
            continue
        bits = []
        for entry in r["bits"].split(";"):
            if not entry:
                continue
            register, _, bit = entry.partition(":")
            if not bit:
                raise SystemExit(
                    f"{r['series']} {r['selector']}: the bits column is "
                    f"{r['bits']!r}, which names no register. device-data "
                    f"changed shape; see "
                    f"docs/research/signal-name-normalization.ja.md")
            bits.append((register, int(bit)))
        if bits:
            kind = m.group(1) if m.group(1) in ("i2c", "spi") else "usart"
            out[(r["series"], kind, int(m.group(2)))] = bits
    return out


def remap_mask_value(bits: list, value: int) -> dict:
    """{register: (mask, value)} for one route, from an LSB-first bit list."""
    out: dict = {}
    for i, (register, bit) in enumerate(bits):
        mask, val = out.get(register, (0, 0))
        mask |= 1 << bit
        if (value >> i) & 1:
            val |= 1 << bit
        out[register] = (mask, val)
    return out


def route_remap_value(route: str):
    """AFIO field value for a pin_functions route, or None if the route is not
    an AFIO remap (the af-N families use a per-pin selector instead)."""
    if route in ("default", "main"):
        return 0
    if route.startswith("remap-"):
        return int(route.split("-", 1)[1])
    return None


def load_forbidden_pads(tables: pathlib.Path) -> dict:
    """part -> pads a generated default must never claim.

    The debug pins (SDI: SWCLK/SWDIO) and the system straps (NRST, BOOT):
    putting Wire or Serial there by default would cut the debug connection or
    fight the boot circuit the moment begin() runs. pin_roles.csv names them
    per part, normalised port/pin columns included, for all 26 series.
    """
    forbidden: dict = {}
    for r in read_table(tables, "pin_roles.csv"):
        if r["peripheral"] == "SDI" or (
                r["peripheral"] == "SYS"
                and r["role"] in ("NRST", "BOOT0", "BOOT1")):
            if r.get("port") and r.get("pin"):
                forbidden.setdefault(r["part_number"], set()).add(
                    (r["port"], int(r["pin"])))
    return forbidden


def resolve_pin_candidates(by_board: dict, kinds: list):
    """Collapse each role's candidate list to one pad, per board.

    Where the tables offer several pads for one (instance, route, role) - 200
    combinations at the current pin - prefer a pad bonded on every part of
    the board, so the ANY menu entry's promise holds (this is what brings
    CH32V205's I2C2 back: PB10/PB11 exist on all four parts, the PC13/PC14
    alternative does not); among equally-covering candidates, the last in
    table order - the old behaviour, kept as the tie-break until upstream's
    preferred-pad mark exists. A plain "lowest pad number" would be stable
    but wrong: V307's SPI3_MOSI offers PB5 and PA15, and PA15 is also
    SPI3_NSS.

    The debug/strap-pad exclusion deliberately does NOT happen here: it
    applies only to what gets chosen as a *default* (choose_routes). The
    setRoute()/setPins() tables keep those routes, because selecting one
    explicitly is a legitimate act on a board that has disabled SWD or NRST.

    Decided with the maintainer on 2026-08-25. Mutates the pin dicts in
    place so every consumer keeps seeing a single pad per role.
    """
    for parts_rows in by_board.values():
        parts = [r["part_number"] for r in parts_rows]
        for pins in kinds:
            keys = sorted({k for pn in parts for k in pins.get(pn, {})})
            for key in keys:
                roles = sorted({role for pn in parts
                                for role in pins.get(pn, {}).get(key, {})})
                for role in roles:
                    per_part = {pn: pins[pn][key][role] for pn in parts
                                if role in pins.get(pn, {}).get(key, {})}
                    merged: list = []
                    for cands in per_part.values():
                        for c in cands:
                            if c not in merged:
                                merged.append(c)
                    uniform = [c for c in merged
                               if all(c in cands for cands in per_part.values())]
                    chosen = (uniform or merged)[-1]
                    for pn, cands in per_part.items():
                        pins[pn][key][role] = (chosen if chosen in cands
                                               else cands[-1])

    # Parts outside every configured board still hold candidate lists; give
    # them the plain table-order tail so nothing downstream meets a list.
    for pins in kinds:
        for part_pins in pins.values():
            for roles in part_pins.values():
                for role, value in list(roles.items()):
                    if isinstance(value, list):
                        roles[role] = value[-1]


def load_pin_routes(tables: pathlib.Path, signal_res: list,
                    route_order: tuple, kind: str = "", alts=None) -> dict:
    """part -> {(instance index, route): {role: (port, bit)}}.

    The roles of one peripheral are kept per route: several families expose an
    instance only through alternate-function routes, and pairing a TX from one
    route with an RX from another would name two pins that cannot be active at
    the same time. The same is true of SCL and SDA.

    **One (instance, route, role) can name several pads, and that is normal.**
    Reverse lookup is one-to-many for 984 of 22453 combinations, measured
    upstream on 2026-08-25: 860 of them af-N, 101 default, 23 remap-N. On the
    af-N families the selector is per pin, so several pads offering the same
    role are alternatives rather than a contradiction; only a remap-N route is
    exclusive, because one field value moves a whole set of pads at once.

    So a second pad is not an error here. The last one is kept, as it always
    has been, and the others are recorded in `alts` so that the choice is
    visible instead of silent: main() reports them and gen_pins() writes them
    into the variant header beside the pin it picked.
    """
    functions = read_table(tables, "pin_functions.csv")
    out: dict = {}
    for r in functions:
        for pattern, extract in signal_res:
            m = pattern.match(r["signal"])
            if m:
                index, role = extract(m)
                break
        else:
            continue
        if r["route"] not in route_order:
            continue
        m = PAD_PORT_RE.match(r["pad"])
        if not m:
            continue
        pad = (m.group(1), int(m.group(2)))
        roles = out.setdefault(r["part_number"], {}).setdefault(
            (index, r["route"]), {})
        candidates = roles.setdefault(role, [])
        if pad not in candidates:
            candidates.append(pad)
        if alts is not None and len(candidates) > 1:
            seen = alts.setdefault(
                (kind, r["part_number"], index, r["route"], role), [])
            for one in candidates:
                if one not in seen:
                    seen.append(one)
    return out


def load_uart_pins(tables: pathlib.Path, alts=None) -> dict:
    """part -> {(usart index, route): {"TX": ..., "RX": ...}}."""
    return load_pin_routes(tables, UART_SIGNAL_RE, UART_ROUTE_ORDER,
                           "uart", alts)


def load_i2c_pins(tables: pathlib.Path, alts=None) -> dict:
    """part -> {(i2c index, route): {"SCL": ..., "SDA": ...}}."""
    return load_pin_routes(tables, I2C_SIGNAL_RE, I2C_ROUTE_ORDER,
                           "i2c", alts)


def load_spi_pins(tables: pathlib.Path, alts=None) -> dict:
    """part -> {(spi index, route): {"SCK": ..., "MISO": ..., "MOSI": ...}}."""
    return load_pin_routes(tables, SPI_SIGNAL_RE, SPI_ROUTE_ORDER,
                           "spi", alts)


def load_dac_pins(tables: pathlib.Path, alts=None) -> dict:
    """part -> {(dac channel, route): {"OUT": (port, bit)}}."""
    return load_pin_routes(tables, DAC_SIGNAL_RE, DAC_ROUTE_ORDER,
                           "dac", alts)


def choose_routes(series: str, parts: list, pins: dict, remap: dict, kind: str,
                  roles: tuple, route_order: tuple, indices=None,
                  forbidden: dict = {}) -> dict:
    """Pick one route per peripheral instance for the whole series.

    Route order comes first, coverage second. Picking by coverage looked
    better on paper - a remap route often reaches more packages - but boards
    are wired to the reset-default pins: every board on the bench is, and so
    are the WCH and the old community cores. A default that needs an AFIO
    remap would be wrong on the hardware people actually have.

    Parts that do not bond the chosen pins have no peripheral there, which the
    generated header states per instance.
    """
    chosen: dict = {}
    present = {i for pn in parts for (i, _r) in pins.get(pn, {})}
    for index in sorted(present):
        if indices is not None and index not in indices:
            continue
        best = None
        for route in route_order:
            pads_by_part = {}
            for pn in parts:
                entry = pins.get(pn, {}).get((index, route))
                if entry and set(roles) <= set(entry):
                    pads = tuple(entry[role] for role in roles)
                    # A debug or strap pad is never a *default*: Wire.begin()
                    # landing on SWDIO cuts the debug connection, and SPI SCK
                    # on a package whose NRST sits on that pad (V006 E8R6
                    # puts NRST on PC5) resets the chip. Per part, because
                    # that is a per-package fact - the part is treated like
                    # one that does not bond the pad, and drops out of this
                    # route's coverage. The route itself stays available in
                    # the setRoute() tables, where choosing it is an explicit
                    # act (2026-08-25 decision).
                    if any(pad in forbidden.get(pn, ()) for pad in pads):
                        continue
                    pads_by_part[pn] = pads
            if not pads_by_part:
                continue
            variants = set(pads_by_part.values())
            if len(variants) != 1:
                continue   # the route moves between packages: unusable for ANY
            coverage = len(pads_by_part)
            # A route the core cannot actually select is worse than any it
            # can: non-default routes need an AFIO field, and device-data does
            # not have one for every series (X033/X035 USART1, for example),
            # while the af-N families use a different mechanism.
            value = route_remap_value(route)
            programmable = value == 0 or (value is not None and
                                          (series, kind, index) in remap)
            score = (programmable, -route_order.index(route), coverage)
            if best is None or score > best[0]:
                best = (score, coverage, route, next(iter(variants)))
        if best:
            chosen[index] = best[1:]
    return chosen


# Every AFIO-selectable route, for the setRoute()/setPins() tables. Wider than
# the *_ROUTE_ORDER tuples above, which exist to rank one default: here the
# point is to list everything the core can actually select, so af-N is absent
# (no AFIO field) and the remap numbers run as far as any series goes.
ROUTE_TABLE_ORDER = ("default", "main") + tuple(f"remap-{i}" for i in range(1, 8))


def route_table(series: str, parts: list, pins: dict, remap: dict, kind: str,
                roles: tuple, index: int) -> list:
    """[(route number, (pads in role order), PCFR1 value, PCFR2 value)].

    Empty when device-data has no AFIO field for the instance: without the
    field the core cannot select a route, and a table it cannot act on would
    only invite setRoute() to lie.
    """
    bits = remap.get((series, kind, index))
    if not bits:
        return []
    rows = []
    for route in ROUTE_TABLE_ORDER:
        value = route_remap_value(route)
        if value is None:
            continue
        pads_by_part = {}
        for pn in parts:
            entry = pins.get(pn, {}).get((index, route))
            if entry and set(roles) <= set(entry):
                pads_by_part[pn] = tuple(entry[r] for r in roles)
        variants = set(pads_by_part.values())
        if len(variants) != 1:
            continue          # absent, or it moves between packages
        mv = remap_mask_value(bits, value)
        rows.append((value, next(iter(variants)),
                     mv.get("PCFR1", (0, 0))[1], mv.get("PCFR2", (0, 0))[1]))
    return rows


def emit_routes(out: list, prefix: str, roles: tuple, rows: list) -> None:
    """The CH32_<prefix>_ROUTES initializer for ch32_route_t[]."""
    if not rows:
        return
    out.append(f"/* {prefix} routes for setRoute()/setPins(): "
               f"route number, then {', '.join(roles)} */")
    out.append(f"#define CH32_{prefix}_ROUTE_COUNT {len(rows)}")
    out.append(f"#define CH32_{prefix}_ROUTES {{ \\")
    for value, pads, v1, v2 in rows:
        names = [pad_name(*p) for p in pads]
        names += ["CH32_ROUTE_NO_PIN"] * (3 - len(names))
        out.append(f"    {{ {value}, {{ {', '.join(names)} }}, "
                   f"0x{v1:08x}u, 0x{v2:08x}u }}, \\")
    out.append("}")


def choose_uarts(series: str, parts: list, uarts: dict, handler_of: dict,
                 remap: dict, forbidden: dict = {}) -> dict:
    """One route per USART, for the USARTs the core can actually drive."""
    usable = {i for i in SERIAL_BASES if i in handler_of}
    return choose_routes(series, parts, uarts, remap, "usart", ("TX", "RX"),
                         UART_ROUTE_ORDER, usable, forbidden)


def choose_i2cs(series: str, parts: list, i2cs: dict, remap: dict,
                forbidden: dict = {}) -> dict:
    """One route per I2C instance. Only the two the register map covers."""
    return choose_routes(series, parts, i2cs, remap, "i2c", ("SCL", "SDA"),
                         I2C_ROUTE_ORDER, set(I2C_BASES), forbidden)


def choose_spis(series: str, parts: list, spis: dict, remap: dict,
                forbidden: dict = {}) -> dict:
    """One route per SPI instance."""
    return choose_routes(series, parts, spis, remap, "spi",
                         ("SCK", "MISO", "MOSI"), SPI_ROUTE_ORDER,
                         set(SPI_BASES), forbidden)


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
    functions = read_table(tables, "pin_functions.csv")
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
             i2cs: dict, spis: dict, dacs: dict, pwm: dict, handlers: list,
             remap: dict, route_alts: dict = None,
             wide_by_family: dict = {},
             forbidden: dict = {}) -> str:
    """Variant pin map for one series (ADR-0010)."""
    parts = sorted(r["part_number"] for r in rows)

    def alternatives(kind: str, index, route: str, roles: tuple) -> list:
        """Comment naming the pads device-data offers and this file did not.

        Silence here used to mean "there was only one pad", and that was wrong
        often enough to matter: on the af-N families the selector is per pin,
        so a role commonly has two or three pads and the loader kept whichever
        came last in the CSV. Writing the others down makes the pick reviewable
        in the diff instead of invisible.
        """
        if not route_alts:
            return []
        lines = []
        for role in roles:
            found = []
            for part in parts:
                for pad in route_alts.get((kind, part, index, route, role), []):
                    if pad not in found:
                        found.append(pad)
            if len(found) > 1:
                names = ", ".join(pad_name(*pad) for pad in found)
                lines.append(f"/* device-data lists {names} for {role} on this route;")
                lines.append(" * the selection rule picked the one above (never a debug")
                lines.append(" * or strap pad, prefer a pad on every part, else table")
                lines.append(" * order - see resolve_pin_candidates). */")
        return lines
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
        f" * {SOURCE_LINE}",
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
    chosen = choose_uarts(series, parts, uarts, handler_of, remap, forbidden)
    if chosen:
        out.append("/* ---- USART pins (device-data; one route per USART, chosen for")
        out.append(" *      the whole series - see choose_uarts in generate.py) ---- */")
        for index, (coverage, route, (tx, rx)) in sorted(chosen.items()):
            where = ("on every part" if coverage == len(parts)
                     else f"on {coverage} of {len(parts)} parts")
            out.append(f"/* USART{index}: route {route}, {where} */")
            out.append(f"#define CH32_SERIAL{index}_TX {pad_name(*tx)}")
            out.append(f"#define CH32_SERIAL{index}_RX {pad_name(*rx)}")
            out.extend(alternatives("uart", index, route, ("TX", "RX")))
            name = handler_of[index]
            out.append(f"#define CH32_SERIAL{index}_HANDLER {name}")
            out.append(f"#define CH32_SERIAL{index}_IRQ "
                       f"CH32_IRQN_{name.removesuffix('_IRQHandler')}")
            value = route_remap_value(route)
            bits = remap.get((series, "usart", index))
            if value is None:
                out.append(f"/* NOTE: route {route} is a per-pin alternate-function")
                out.append(" * selector, not an AFIO remap. The core does not program it")
                out.append(" * yet, so this port needs verifying (docs/todo.ja.md). */")
            elif bits:
                # Emitted for the default route (value 0) too, so begin()
                # writes the field rather than assuming it. Re-initialising a
                # port back onto its default pins is ordinary use, and "leave
                # the field alone" makes that a no-op that silently keeps the
                # previous route - which is exactly how the uart_scan sweep
                # ended up driving every route out of one pad.
                #
                # One macro pair per register the field spans. PCFR2 is not
                # cosmetic: where the field crosses into it, writing PCFR1
                # alone lands on a different route.
                for register, (mask, val) in sorted(
                        remap_mask_value(bits, value).items()):
                    suffix = "" if register == "PCFR1" else "2"
                    out.append(f"#define CH32_SERIAL{index}_REMAP{suffix}_MASK "
                               f"0x{mask:08x}u")
                    out.append(f"#define CH32_SERIAL{index}_REMAP{suffix}_VAL  "
                               f"0x{val:08x}u")
            elif value:
                out.append(f"/* NOTE: route {route} needs an AFIO remap but device-data")
                out.append(f" * has no AFIO field for USART{index} in this series. */")
            emit_routes(out, f"SERIAL{index}", ("TX", "RX"),
                        route_table(series, parts, uarts, remap, "usart",
                                    ("TX", "RX"), index))
        # Serial points at the USART that reaches the most part numbers.
        # Serial is the lowest-numbered USART on its reset-default pins;
        # that is what a board's silkscreen almost always means by "UART".
        def rank(i):
            coverage, route, _pads = chosen[i]
            value = route_remap_value(route)
            programmable = value == 0 or (value is not None and
                                          (series, "usart", i) in remap)
            return (programmable, route in ("default", "main"), -i)
        default = max(sorted(chosen), key=rank)
        # A board can wire a different USART than the series-wide choice: the
        # generator optimises for the ANY entry (pins present on every part),
        # while a real board only has to work for itself. Overridable with
        # -DCH32_SERIAL_DEFAULT=<n>, which is what the uart_scan manual
        # test reports.
        out.append("#ifndef CH32_SERIAL_DEFAULT")
        out.append(f"#define CH32_SERIAL_DEFAULT {default}")
        out.append("#endif")
        out.append("")

    # --- I2C ---
    # Same shape as the USART block, and for the same reason: the pair has to
    # come from one route, and the route has to be one the core can select.
    chosen_i2c = choose_i2cs(series, parts, i2cs, remap, forbidden)
    if chosen_i2c:
        out.append("/* ---- I2C pins (device-data; one route per instance,")
        out.append(" *      chosen for the whole series - see choose_i2cs) ---- */")
        for index, (coverage, route, (scl, sda)) in sorted(chosen_i2c.items()):
            where = ("on every part" if coverage == len(parts)
                     else f"on {coverage} of {len(parts)} parts")
            out.append(f"/* I2C{index}: route {route}, {where} */")
            out.append(f"#define CH32_I2C{index}_SCL {pad_name(*scl)}")
            out.append(f"#define CH32_I2C{index}_SDA {pad_name(*sda)}")
            out.extend(alternatives("i2c", index, route, ("SCL", "SDA")))
            value = route_remap_value(route)
            bits = remap.get((series, "i2c", index))
            if value is None:
                out.append(f"/* NOTE: route {route} is a per-pin alternate-function")
                out.append(" * selector, not an AFIO remap. The core does not program it")
                out.append(" * yet, so this instance needs verifying (docs/todo.ja.md). */")
            elif bits:
                for register, (mask, val) in sorted(
                        remap_mask_value(bits, value).items()):
                    suffix = "" if register == "PCFR1" else "2"
                    out.append(f"#define CH32_I2C{index}_REMAP{suffix}_MASK "
                               f"0x{mask:08x}u")
                    out.append(f"#define CH32_I2C{index}_REMAP{suffix}_VAL  "
                               f"0x{val:08x}u")
            elif value:
                out.append(f"/* NOTE: route {route} needs an AFIO remap but device-data")
                out.append(f" * has no AFIO field for I2C{index} in this series. */")
            emit_routes(out, f"I2C{index}", ("SCL", "SDA"),
                        route_table(series, parts, i2cs, remap, "i2c",
                                    ("SCL", "SDA"), index))
        # No CH32_WIRE_DEFAULT to match CH32_SERIAL_DEFAULT: the Arduino
        # ecosystem names I2C buses Wire/Wire1 in bus order, so the library
        # binds the bare name to the first instance itself.
        #
        # The standard aliases point at that same first instance. Libraries
        # written for other cores use PIN_WIRE_SDA or the bare SDA, and a core
        # that defines neither simply fails to compile them.
        first = min(chosen_i2c)
        out.append("/* Arduino's standard names for the first bus (Wire). */")
        out.append("#ifndef PIN_WIRE_SCL")
        out.append(f"#define PIN_WIRE_SCL CH32_I2C{first}_SCL")
        out.append(f"#define PIN_WIRE_SDA CH32_I2C{first}_SDA")
        out.append("#define SCL PIN_WIRE_SCL")
        out.append("#define SDA PIN_WIRE_SDA")
        out.append("#endif")
        out.append("")

    # --- SPI ---
    chosen_spi = choose_spis(series, parts, spis, remap, forbidden)
    if chosen_spi:
        out.append("/* ---- SPI pins (device-data; one route per instance,")
        out.append(" *      chosen for the whole series - see choose_spis).")
        out.append(" *      NSS is not listed: Arduino drives chip select as a GPIO. ---- */")
        for index, (coverage, route, (sck, miso, mosi)) in sorted(chosen_spi.items()):
            where = ("on every part" if coverage == len(parts)
                     else f"on {coverage} of {len(parts)} parts")
            out.append(f"/* SPI{index}: route {route}, {where} */")
            out.append(f"#define CH32_SPI{index}_SCK {pad_name(*sck)}")
            out.append(f"#define CH32_SPI{index}_MISO {pad_name(*miso)}")
            out.append(f"#define CH32_SPI{index}_MOSI {pad_name(*mosi)}")
            out.extend(alternatives("spi", index, route,
                                    ("SCK", "MISO", "MOSI")))
            value = route_remap_value(route)
            bits = remap.get((series, "spi", index))
            if value is None:
                out.append(f"/* NOTE: route {route} is a per-pin alternate-function")
                out.append(" * selector, not an AFIO remap. The core does not program it")
                out.append(" * yet, so this instance needs verifying (docs/todo.ja.md). */")
            elif bits:
                for register, (mask, val) in sorted(
                        remap_mask_value(bits, value).items()):
                    suffix = "" if register == "PCFR1" else "2"
                    out.append(f"#define CH32_SPI{index}_REMAP{suffix}_MASK "
                               f"0x{mask:08x}u")
                    out.append(f"#define CH32_SPI{index}_REMAP{suffix}_VAL  "
                               f"0x{val:08x}u")
            elif value:
                out.append(f"/* NOTE: route {route} needs an AFIO remap but device-data")
                out.append(f" * has no AFIO field for SPI{index} in this series. */")
            emit_routes(out, f"SPI{index}", ("SCK", "MISO", "MOSI"),
                        route_table(series, parts, spis, remap, "spi",
                                    ("SCK", "MISO", "MOSI"), index))
        first = min(chosen_spi)
        out.append("/* Arduino's standard names for the first bus (SPI). */")
        out.append("#ifndef PIN_SPI_SCK")
        out.append(f"#define PIN_SPI_SCK CH32_SPI{first}_SCK")
        out.append(f"#define PIN_SPI_MISO CH32_SPI{first}_MISO")
        out.append(f"#define PIN_SPI_MOSI CH32_SPI{first}_MOSI")
        out.append("#define SCK PIN_SPI_SCK")
        out.append("#define MISO PIN_SPI_MISO")
        out.append("#define MOSI PIN_SPI_MOSI")
        # SS is the peripheral's own NSS pad on the route that was chosen. The
        # driver never uses it - chip select is a GPIO - but libraries expect
        # the name to exist, and this is the pad their wiring diagrams show.
        nss = None
        _cov, chosen_route, _pads = chosen_spi[first]
        for pn in parts:
            entry = spis.get(pn, {}).get((first, chosen_route))
            if entry and "NSS" in entry:
                nss = entry["NSS"]
                break
        if nss is not None:
            out.append(f"#define PIN_SPI_SS {pad_name(*nss)}")
            out.append("#define SS PIN_SPI_SS")
        else:
            out.append("/* No SS: device-data names no NSS pad on this route. */")
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

    # --- DAC ---
    # analogWrite() on one of these pads has to reach the converter instead of
    # a timer, so the pad is all the core needs to know.
    chosen_dac = choose_routes(series, parts, dacs, remap, "dac", ("OUT",),
                               DAC_ROUTE_ORDER, forbidden=forbidden)
    if chosen_dac:
        out.append("/* ---- DAC: analogWrite() drives the converter on these pads,")
        out.append(" *      not a timer. ---- */")
        for channel, (coverage, route, (pad,)) in sorted(chosen_dac.items()):
            where = ("on every part" if coverage == len(parts)
                     else f"on {coverage} of {len(parts)} parts")
            out.append(f"/* DAC{channel}: {where} */")
            out.append(f"#define CH32_DAC{channel}_PIN {pad_name(*pad)}")
        out.append("")

    # --- tone() and Servo timers ---
    # Both need a timer whose *update* interrupt has a vector of its own, and
    # they must not be the same timer: sounding a buzzer while a servo moves is
    # ordinary. A timer qualifies either through a single TIMn vector or
    # through a separate TIMn_UP one - the advanced timers split their
    # interrupt four ways, and the update part is what these two use.
    #
    # Preference is for a timer no PWM pad uses. Where there is none the choice
    # is still made, and the header says which analogWrite() pads stop working,
    # exactly as the AVR core documents for pins 3, 9, 10 and 11.
    candidates = {}
    for name in handlers:
        if not name:
            continue
        m = re.match(r"^TIM(\d+)(_UP)?_IRQHandler$", name)
        if not m:
            continue
        number = int(m.group(1))
        # Only the timers ch32_registers.h names. V30x/V4x7 also have TIM8..10
        # on APB2, which nothing in the core can address yet (docs/todo.ja.md).
        if number > 7:
            continue
        # A whole-timer vector is preferred over the update-only one when a
        # family somehow has both.
        if number not in candidates or not m.group(2):
            candidates[number] = name
    pwm_timers = {tc[0] for tc in pwm_pads.values()}
    # The 32-bit timer, if this family has one. A series can span two families
    # (CH32V203 has one CH32V205 part among twelve CH32V20x ones), and the
    # variant is built for all of them, so the union is what the header must
    # describe - a 32-bit store is right on the wide part and harmless on the
    # narrow one (measured on CH32V203C8T6), while a 16-bit store is wrong on
    # the wide one (measured on CH32L103: it silenced tone()).
    wide_timers = set()
    for family in {r.get("family", "") for r in rows}:
        wide_timers |= wide_by_family.get(family, set())

    def pick(pool):
        """Highest-numbered, preferring one no PWM pad uses."""
        free = [t for t in pool if t not in pwm_timers]
        return (free or pool or [None])[-1]

    def emit_timer(kind: str, number, users: str):
        if number is None:
            out.append(f"/* No timer left for {kind}: it is unavailable on this series. */")
            out.append("")
            return
        handler = candidates[number]
        irqn = handler.removesuffix("_IRQHandler")
        shared = number in pwm_timers
        if shared:
            pads = sorted(pad_name(*k) for k, v in pwm_pads.items()
                          if v[0] == number)
            out.append(f"/* ---- {kind}: TIM{number}, which is also a PWM timer here, so")
            out.append(f" *      analogWrite() on {', '.join(pads)}")
            out.append(f" *      is disturbed while {users}. ---- */")
        else:
            out.append(f"/* ---- {kind}: TIM{number}, free of PWM pads. ---- */")
        prefix = "CH32_TONE" if kind == "tone()" else "CH32_SERVO"
        bus = "APB2" if number == 1 else "APB1"
        out.append(f"#define {prefix}_TIMER {number}")
        out.append(f"#define {prefix}_TIMER_BASE CH32_TIM{number}_BASE")
        out.append(f"#define {prefix}_TIMER_RCC CH32_RCC_{bus}_TIM{number}")
        out.append(f"#define {prefix}_TIMER_ON_APB2 {1 if number == 1 else 0}")
        out.append(f"#define {prefix}_TIMER_IRQ CH32_IRQN_{irqn}")
        out.append(f"#define {prefix}_TIMER_HANDLER {handler}")
        out.append(f"#define {prefix}_SHARES_PWM {1 if shared else 0}")
        if number in wide_timers:
            out.append(f"/* CNT and ATRLR are 32 bit on this timer: a 16-bit"
                       f" store would be")
            out.append(f" * replicated into both halves. See ch32_registers.h. */")
        out.append(f"#define {prefix}_TIMER_BITS "
                   f"{32 if number in wide_timers else 16}")
        out.append("")

    tone_timer = pick(sorted(candidates))
    emit_timer("tone()", tone_timer, "a tone plays")
    emit_timer("Servo", pick([t for t in sorted(candidates) if t != tone_timer]),
               "a servo is attached")

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


def clock_defines(clk: dict) -> str:
    """The resolved clock setting, as the -D values SystemInit reads."""
    return (f"-DCH32_CLOCK_SYSCLK_HZ={clk['sysclk']} "
            f"-DCH32_CLOCK_USE_PLL={1 if clk['config'] else 0} "
            f"-DCH32_CLOCK_PLL_MASK={clk['pll_mask']:#x}u "
            f"-DCH32_CLOCK_PLL_VALUE={clk['pll_value']:#x}u "
            f"-DCH32_CLOCK_EXTEN_ADDR={clk['exten_addr']:#x}u "
            f"-DCH32_CLOCK_EXTEN_BITS={clk['exten_bits']:#x}u")


def gen_board(series: str, rows: list, probe_rs: set, facts: dict,
              die: dict, clock_for):
    """One board per series. Returns (boards.txt block, {ld name: content})."""
    cfg = SERIES_CONFIG[series]
    fam = FAMILY[cfg["family"]]
    fact = facts[cfg["family"]]
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
    # One stem, three file names: platform.txt builds vectors_*.inc, irqn_*.h
    # and exti_*.h from it. Keeping it a single property is what lets a menu
    # entry move a part to another die variant in one line.
    lines.append(f"{board}.build.vector_variant={cfg['vectors']}")
    lines.append(f"{board}.build.clock_init=clock_init_{cfg['family'].lower()}.h")
    # Wait states belong to the clock, not to the family: CH32V103 needs 0 at
    # 8 MHz and 2 at 72. Taken from the configuration that was resolved for
    # this F_CPU; families whose setters never write ACTLR fall back to the
    # value in FAMILY, which is 0 and never reaches the register (their
    # LATENCY mask is 0).
    clk = clock_for()
    latency = clk["latency"] if clk["latency"] is not None else fam["flash_latency"]
    lines.append(
        f"{board}.build.core_defines="
        f"-DCH32_GPIO_PORT_WIDTH={fact['port_width']} "
        f"-DCH32_SYSTICK_64={fam['systick64']} "
        f"-DCH32_SYSTICK_V103={1 if cfg['family'] == 'CH32V103' else 0} "
        f"-DCH32_HSI_HZ={fact['hsi_hz']} "
        f"-DCH32_FLASH_LATENCY={latency} "
        f"-DCH32_ADC_BITS={fam['adc_bits']} "
        f"-DCH32_I2C_HAS_RTR={fam['i2c_has_rtr']} "
        f"-DCH32_HPRE_LINEAR={fact['hpre_linear']} "
        f"-DCH32_FLASH_ACTLR_LATENCY_MASK={fact['latency_mask']:#x}u "
        f"-DCH32_ADC_MAX_HZ={fact['adc_max_hz']}u"
        + (f" -DCH32_LSI_HZ={fact['lsi_hz']}u" if fact['lsi_hz'] else "")
        + (f" -DCH32_IWDG_BASE={fact['iwdg_base']:#x}u"
           if fact['iwdg_base'] else ""))
    # Its own property, not part of core_defines: a pnum entry has to be able
    # to replace it outright, and a menu value that referred back to the board
    # value would be referring to itself.
    board_clock = clock_defines(clk)
    lines.append(f"{board}.build.clock_defines={board_clock}")
    lines.append("")

    ld_files = {}

    def ld_for(flash: int, sram: int) -> str:
        name = f"{series.lower()}_{kb(flash)}k_{kb(sram)}k.ld"
        if name not in ld_files:
            ld_files[name] = (
                ld_header()
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
        # ANY deliberately keeps the board's variant: it already declares the
        # smallest flash in the series, so it is the "not a specific part"
        # entry and a part that needs its own table has to be picked by name.
        if pn in die:
            lines.append(f"{pfx}.build.vector_variant={die[pn]}")
            # A different die can also mean a different PLL encoding, so the
            # clock setting is re-resolved for this part. Emitted only when it
            # actually differs, so the menu stays readable.
            part_clock = clock_defines(clock_for(pn))
            if part_clock != board_clock:
                lines.append(f"{pfx}.build.clock_defines={part_clock}")
        lines.append("")

    for key, label, flags in PRINTF_MENU:
        lines.append(f"{board}.menu.printf.{key}={label}")
        lines.append(f"{board}.menu.printf.{key}.build.printf_flags={flags}")
    lines.append("")

    return "\n".join(lines), ld_files


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--tables", required=True, type=pathlib.Path)
    ap.add_argument("--platform", required=True, type=pathlib.Path)
    ap.add_argument("--check", action="store_true",
                    help="verify committed files match regeneration; do not write")
    ap.add_argument("--diff", action="store_true",
                    help="with --check, print a unified diff of each drifting file")
    args = ap.parse_args()
    args.check = args.check or args.diff

    products = read_table(args.tables, "products.csv")
    commit = source_commit(args.tables)

    interrupts, vector_forms = load_interrupts()
    pads, adc, unresolved = load_pin_tables(args.tables)
    # Where one (instance, route, role) names several pads. Not an error - see
    # load_pin_routes - but the pick has to be visible rather than silent.
    route_alts: dict = {}
    uarts = load_uart_pins(args.tables, route_alts)
    i2cs = load_i2c_pins(args.tables, route_alts)
    spis = load_spi_pins(args.tables, route_alts)
    dacs = load_dac_pins(args.tables, route_alts)
    probe_rs = load_probe_rs_targets()
    remap = load_remap_fields(args.tables)
    wide_timers = load_wide_timers(args.tables)
    pwm = load_pwm_pins(args.tables)
    errata_ids = load_errata_ids(args.tables)
    facts = load_family_facts(args.tables, pads, products)
    die = load_die_variants(args.tables)
    die_macro_series, die_macro_part = load_die_macros(args.tables)
    clock_configs_t, clock_symbols_t = load_clock(args.tables)
    clock_init_t = load_clock_init(args.tables)
    disagreements = check_family_facts(args.tables, facts)
    if disagreements:
        print("ERROR: hand-written values disagree with ch32-device-data:",
              file=sys.stderr)
        for line in disagreements:
            print(f"  {line}", file=sys.stderr)
        return 1
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

    resolve_pin_candidates(by_board, [uarts, i2cs, spis, dacs])
    forbidden = load_forbidden_pads(args.tables)

    missing = [s for s in SERIES_CONFIG if s not in by_board]
    if missing:
        print(f"ERROR: no products for series {missing}", file=sys.stderr)
        return 1

    outputs = {}
    boards_blocks = []
    used_variants = set()
    for series in SERIES_CONFIG:
        rows = by_board[series]
        block, ld_files = gen_board(
            series, rows, probe_rs, facts, die,
            lambda part=None, series=series: resolve_clock(
                SERIES_CONFIG[series]["family"],
                int(FAMILY[SERIES_CONFIG[series]["family"]]["f_cpu"].rstrip("L")),
                die_macro_part.get(part) if part else die_macro_series.get(series),
                facts[SERIES_CONFIG[series]["family"]]["hsi_hz"],
                clock_configs_t, clock_symbols_t))
        boards_blocks.append(block)
        used_variants.add(SERIES_CONFIG[series]["vectors"])
        # A part on another die variant needs that table emitted too.
        used_variants.update(die[r["part_number"]] for r in rows
                             if r["part_number"] in die)
        for name, content in ld_files.items():
            outputs[args.platform / "variants" / series / name] = content
        outputs[args.platform / "variants" / series / "pins_arduino.h"] = \
            gen_pins(series, rows, pads, adc, uarts, i2cs, spis, dacs, pwm,
                     interrupts[SERIES_CONFIG[series]['vectors']], remap,
                     route_alts, wide_timers, forbidden)

    # What the one-to-many lookups resolved, grouped by route kind. Printed
    # every run rather than only on change: "the generator picked one of
    # several" is a standing property of the tables, not an incident, and the
    # counts moving is the signal worth noticing.
    if route_alts:
        by_route: dict = {}
        for (kind, part, index, route, role) in route_alts:
            key = ("af-N" if route.startswith("af-") else
                   "remap-N" if route.startswith("remap-") else route)
            by_route.setdefault(key, []).append((kind, part, index, route, role))
        summary = ", ".join(f"{k} {len(v)}" for k, v in sorted(by_route.items()))
        print(f"note: {len(route_alts)} (instance, route, role) combinations name "
              f"several pads ({summary}); resolved by rule - no debug/strap "
              f"pads, prefer series-wide pads, then table order. Alternatives "
              f"are written into the variant headers.")
        # remap-N is the exclusive kind: one field value moves a whole set of
        # pads, so two pads for one role there is a table problem rather than a
        # choice. Named, not fatal - upstream is working through them (F-27/F-28).
        exclusive = sorted(by_route.get("remap-N", []))
        for kind, part, index, route, role in exclusive:
            pads = ", ".join(pad_name(*x)
                             for x in route_alts[(kind, part, index, route, role)])
            print(f"  WARNING: {part} {kind}{index} {route} {role}: {pads} "
                  f"(a remap route is exclusive; two pads means the table "
                  f"disagrees with itself)")

    generated_parts = {r["part_number"] for rows in by_board.values() for r in rows}
    blocked = sorted(p for p in unresolved if p[0] in generated_parts)
    if blocked:
        print("ERROR: pads with no port assignment in a generated series "
              "(add to NON_PORT_PADS if they are not GPIO port bits): "
              f"{blocked}", file=sys.stderr)
        return 1

    outputs[args.platform / "boards.txt"] = (
        gen_header() + "\n" + MENU_HEADER + "\n" + "\n".join(boards_blocks))

    for variant in sorted(used_variants):
        if variant not in interrupts:
            print(f"ERROR: no interrupt table for variant {variant} "
                  f"(rebuild with tools/generate/import_vectors.py)", file=sys.stderr)
            return 1
        outputs[args.platform / "cores" / "arduino" / f"vectors_{variant}.inc"] = \
            gen_vectors(variant, interrupts[variant],
                        vector_forms[variant])
        outputs[args.platform / "cores" / "arduino" / f"irqn_{variant}.h"] = \
            gen_irqns(variant, interrupts[variant])
        outputs[args.platform / "cores" / "arduino" / f"exti_{variant}.h"] = \
            gen_exti(variant, interrupts[variant])

    for family in sorted({SERIES_CONFIG[s]["family"] for s in SERIES_CONFIG}):
        steps = clock_init_t.get(family)
        if not steps:
            raise SystemExit(f"ERROR: clock_init.csv has no SystemInit steps "
                             f"for {family}")
        outputs[args.platform / "cores" / "arduino" /
                f"clock_init_{family.lower()}.h"] = \
            gen_clock_init(family, steps, clock_symbols_t)

    # Last: gen_lock hashes the tables that were actually read, so every
    # loader above has to have run first.
    if source_is_dirty(args.tables):
        print("WARNING: the tables have uncommitted changes, so the hashes "
              f"about to be written do not describe commit {commit[:12]}",
              file=sys.stderr)
    outputs[args.platform / LOCK_REL] = gen_lock(args.tables, commit)

    drift = 0
    additive, rewritten = [], []
    for path, content in outputs.items():
        if args.check:
            on_disk = path.read_text(encoding="utf-8") if path.exists() else None
            if on_disk != content:
                print(f"DRIFT: {path}")
                diff = list(difflib.unified_diff(
                    (on_disk or "").splitlines(keepends=True),
                    content.splitlines(keepends=True),
                    fromfile=f"a/{path}", tofile=f"b/{path}"))
                # Nothing removed means the tables only gained facts: a new
                # route, pad or part number. Something removed means an
                # existing one moved, which can change what a sketch compiles
                # to. Worth separating, because only the second kind has to be
                # understood before it is adopted. This reading only works
                # because the header carries no commit id: when it did, every
                # file lost a line on every bump and the signal was buried.
                changed = any(ln.startswith("-") and not ln.startswith("---")
                              for ln in diff)
                if path.as_posix().endswith(LOCK_REL):
                    pass          # the pin itself always moves; not a finding
                else:
                    (rewritten if changed else additive).append(path)
                if args.diff:
                    sys.stdout.writelines(diff)
                drift = 1
            else:
                print(f"ok:    {path}")
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content, encoding="utf-8")
            print(f"wrote: {path}")

    if drift:
        print(f"\nadoption summary: {len(additive)} additive, "
              f"{len(rewritten)} rewriting existing lines", file=sys.stderr)
        for path in rewritten:
            print(f"  REVIEW: {path}", file=sys.stderr)
    return drift


if __name__ == "__main__":
    sys.exit(main())
