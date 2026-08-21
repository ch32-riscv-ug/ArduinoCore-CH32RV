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
    "CH32V20x": dict(march="rv32imac_zicsr", mabi="ilp32", f_cpu="8000000L",
                     defines="-DCH32_MSTATUS_INIT=0x88 -DCH32_INTSYSCR_INIT=0x3 "
                             "-DCH32_CORECFGR=0x1f",
                     systick64=1, flash_latency=0, adc_bits=12, i2c_has_rtr=1),
    "CH32V307": dict(march="rv32imafc_zicsr", mabi="ilp32f", f_cpu="8000000L",
                     defines="-DCH32_MSTATUS_INIT=0x6088 -DCH32_INTSYSCR_INIT=0x0b "
                             "-DCH32_CORECFGR=0x1f",
                     systick64=1, flash_latency=0, adc_bits=12, i2c_has_rtr=1),
    "CH32V407": dict(march="rv32imac_zicsr", mabi="ilp32", f_cpu="20000000L",
                     defines="-DCH32_MSTATUS_INIT=0x688 -DCH32_INTSYSCR_INIT=0x07 "
                             "-DCH32_CORECFGR=0x21 -DCH32_CSR_BC1=0x01 -DCH32_CSR805_CLR=0x100",
                     systick64=0, flash_latency=1, adc_bits=12, i2c_has_rtr=1),
    "CH32X035": dict(march="rv32imac_zicsr", mabi="ilp32", f_cpu="48000000L",
                     defines="-DCH32_MSTATUS_INIT=0x88 -DCH32_INTSYSCR_INIT=0x3 "
                             "-DCH32_CORECFGR=0x1f",
                     systick64=1, flash_latency=2, adc_bits=12, i2c_has_rtr=0),
    "CH32X315": dict(march="rv32imafc_zicsr", mabi="ilp32f", f_cpu="20000000L",
                     defines="-DCH32_MSTATUS_INIT=0x6088 -DCH32_INTSYSCR_INIT=0x07 "
                             "-DCH32_CORECFGR=0x123703E1 -DCH32_CSR_BC1=0x01",
                     systick64=0, flash_latency=1, adc_bits=12, i2c_has_rtr=0),
    # CH32V103's table is a jump table and its startup never writes csr 0x804.
    "CH32V103": dict(march="rv32imac_zicsr", mabi="ilp32", f_cpu="8000000L",
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

    # AHB prescaler encoding. The two schemes agree on /1 and differ on /2,
    # which is the cheapest question that separates them: 0x10 counts (/2 is
    # field 1) and 0x80 is a power-of-two field. Reading /2 wrong runs the part
    # at twice the clock every timing calculation assumes.
    hpre: dict = {}
    for r in read_table(tables, "clock_prescalers.csv",
                        ("family", "field", "divider", "value")):
        if r["field"] == "HPRE" and r["divider"] == "2":
            hpre.setdefault(r["family"], set()).add(int(r["value"]))

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
        facts[family] = dict(
            hsi_hz=one(family, "the HSI frequency", set(hsi.get(family, ()))),
            hpre_linear=1 if enc == 0x10 else 0,
            port_width=one(family, "the GPIO port width",
                           {width[family]} if family in width else set()),
        )
    return facts


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
        # A board is one vector table, so any SKU on a different die variant is
        # currently built with the wrong one. Reported, not fatal: fixing it
        # needs per-pnum build.vectors (docs/todo.ja.md).
        for (other, _), parts in sorted(found.items()):
            if other != suffix:
                print(f"WARNING: {series}: {', '.join(sorted(parts))} "
                      f"{'is' if len(parts) == 1 else 'are'} {other.upper()}, "
                      f"not the {suffix.upper()} the board is built as",
                      file=sys.stderr)

    # Flash latency at the clock we boot at. Only checkable where EVT ships a
    # setter for exactly that frequency off HSI; most families reach their
    # default by not configuring anything, so there is nothing to compare to.
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


def load_pin_routes(tables: pathlib.Path, signal_res: list,
                    route_order: tuple) -> dict:
    """part -> {(instance index, route): {role: (port, bit)}}.

    The roles of one peripheral are kept per route: several families expose an
    instance only through alternate-function routes, and pairing a TX from one
    route with an RX from another would name two pins that cannot be active at
    the same time. The same is true of SCL and SDA.
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
        out.setdefault(r["part_number"], {}).setdefault(
            (index, r["route"]), {})[role] = (m.group(1), int(m.group(2)))
    return out


def load_uart_pins(tables: pathlib.Path) -> dict:
    """part -> {(usart index, route): {"TX": ..., "RX": ...}}."""
    return load_pin_routes(tables, UART_SIGNAL_RE, UART_ROUTE_ORDER)


def load_i2c_pins(tables: pathlib.Path) -> dict:
    """part -> {(i2c index, route): {"SCL": ..., "SDA": ...}}."""
    return load_pin_routes(tables, I2C_SIGNAL_RE, I2C_ROUTE_ORDER)


def load_spi_pins(tables: pathlib.Path) -> dict:
    """part -> {(spi index, route): {"SCK": ..., "MISO": ..., "MOSI": ...}}."""
    return load_pin_routes(tables, SPI_SIGNAL_RE, SPI_ROUTE_ORDER)


def load_dac_pins(tables: pathlib.Path) -> dict:
    """part -> {(dac channel, route): {"OUT": (port, bit)}}."""
    return load_pin_routes(tables, DAC_SIGNAL_RE, DAC_ROUTE_ORDER)


def choose_routes(series: str, parts: list, pins: dict, remap: dict, kind: str,
                  roles: tuple, route_order: tuple, indices=None) -> dict:
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
                    pads_by_part[pn] = tuple(entry[role] for role in roles)
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
                 remap: dict) -> dict:
    """One route per USART, for the USARTs the core can actually drive."""
    usable = {i for i in SERIAL_BASES if i in handler_of}
    return choose_routes(series, parts, uarts, remap, "usart", ("TX", "RX"),
                         UART_ROUTE_ORDER, usable)


def choose_i2cs(series: str, parts: list, i2cs: dict, remap: dict) -> dict:
    """One route per I2C instance. Only the two the register map covers."""
    return choose_routes(series, parts, i2cs, remap, "i2c", ("SCL", "SDA"),
                         I2C_ROUTE_ORDER, set(I2C_BASES))


def choose_spis(series: str, parts: list, spis: dict, remap: dict) -> dict:
    """One route per SPI instance."""
    return choose_routes(series, parts, spis, remap, "spi",
                         ("SCK", "MISO", "MOSI"), SPI_ROUTE_ORDER,
                         set(SPI_BASES))


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
             remap: dict) -> str:
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
    chosen_i2c = choose_i2cs(series, parts, i2cs, remap)
    if chosen_i2c:
        out.append("/* ---- I2C pins (device-data; one route per instance,")
        out.append(" *      chosen for the whole series - see choose_i2cs) ---- */")
        for index, (coverage, route, (scl, sda)) in sorted(chosen_i2c.items()):
            where = ("on every part" if coverage == len(parts)
                     else f"on {coverage} of {len(parts)} parts")
            out.append(f"/* I2C{index}: route {route}, {where} */")
            out.append(f"#define CH32_I2C{index}_SCL {pad_name(*scl)}")
            out.append(f"#define CH32_I2C{index}_SDA {pad_name(*sda)}")
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
    chosen_spi = choose_spis(series, parts, spis, remap)
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
                               DAC_ROUTE_ORDER)
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


def gen_board(series: str, rows: list, probe_rs: set, facts: dict):
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
    lines.append(f"{board}.build.vectors=vectors_{cfg['vectors']}.inc")
    lines.append(
        f"{board}.build.core_defines="
        f"-DCH32_GPIO_PORT_WIDTH={fact['port_width']} "
        f"-DCH32_SYSTICK_64={fam['systick64']} "
        f"-DCH32_HSI_HZ={fact['hsi_hz']} "
        f"-DCH32_FLASH_LATENCY={fam['flash_latency']} "
        f"-DCH32_ADC_BITS={fam['adc_bits']} "
        f"-DCH32_I2C_HAS_RTR={fam['i2c_has_rtr']} "
        f"-DCH32_HPRE_LINEAR={fact['hpre_linear']} "
        f"-DCH32_IRQNS=irqn_{cfg['vectors']}.h "
        f"-DCH32_EXTIS=exti_{cfg['vectors']}.h")
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
    uarts = load_uart_pins(args.tables)
    i2cs = load_i2c_pins(args.tables)
    spis = load_spi_pins(args.tables)
    dacs = load_dac_pins(args.tables)
    probe_rs = load_probe_rs_targets()
    remap = load_remap_fields(args.tables)
    pwm = load_pwm_pins(args.tables)
    errata_ids = load_errata_ids(args.tables)
    facts = load_family_facts(args.tables, pads, products)
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

    missing = [s for s in SERIES_CONFIG if s not in by_board]
    if missing:
        print(f"ERROR: no products for series {missing}", file=sys.stderr)
        return 1

    outputs = {}
    boards_blocks = []
    used_variants = set()
    for series in SERIES_CONFIG:
        rows = by_board[series]
        block, ld_files = gen_board(series, rows, probe_rs, facts)
        boards_blocks.append(block)
        used_variants.add(SERIES_CONFIG[series]["vectors"])
        for name, content in ld_files.items():
            outputs[args.platform / "variants" / series / name] = content
        outputs[args.platform / "variants" / series / "pins_arduino.h"] = \
            gen_pins(series, rows, pads, adc, uarts, i2cs, spis, dacs, pwm,
                     interrupts[SERIES_CONFIG[series]['vectors']], remap)

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
