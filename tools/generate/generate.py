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
                     defines="-DCH32_MSTATUS_INIT=0x1880 -DCH32_INTSYSCR_INIT=0x3 -DCH32_HIGHCODE"),
    "CH32V006": dict(march="rv32emc_zicsr", mabi="ilp32e", f_cpu="24000000L",
                     defines="-DCH32_MSTATUS_INIT=0x1880 -DCH32_INTSYSCR_INIT=0x3"),
    "CH32V205": dict(march="rv32imc_zicsr", mabi="ilp32", f_cpu="8000000L",
                     defines="-DCH32_MSTATUS_INIT=0x88 -DCH32_INTSYSCR_INIT=0x7 "
                             "-DCH32_CORECFGR=0x21 -DCH32_CSR_BC1=0x1"),
    "CH32V20x": dict(march="rv32imac_zicsr", mabi="ilp32", f_cpu="8000000L",
                     defines="-DCH32_MSTATUS_INIT=0x88 -DCH32_INTSYSCR_INIT=0x3 "
                             "-DCH32_CORECFGR=0x1f"),
    "CH32V307": dict(march="rv32imafc_zicsr", mabi="ilp32f", f_cpu="8000000L",
                     defines="-DCH32_MSTATUS_INIT=0x6088 -DCH32_INTSYSCR_INIT=0x0b "
                             "-DCH32_CORECFGR=0x1f"),
    "CH32V407": dict(march="rv32imac_zicsr", mabi="ilp32", f_cpu="20000000L",
                     defines="-DCH32_MSTATUS_INIT=0x688 -DCH32_INTSYSCR_INIT=0x07 "
                             "-DCH32_CORECFGR=0x21 -DCH32_CSR_BC1=0x01 -DCH32_CSR805_CLR=0x100"),
    "CH32X035": dict(march="rv32imac_zicsr", mabi="ilp32", f_cpu="48000000L",
                     defines="-DCH32_MSTATUS_INIT=0x88 -DCH32_INTSYSCR_INIT=0x3 "
                             "-DCH32_CORECFGR=0x1f"),
    "CH32X315": dict(march="rv32imafc_zicsr", mabi="ilp32f", f_cpu="20000000L",
                     defines="-DCH32_MSTATUS_INIT=0x6088 -DCH32_INTSYSCR_INIT=0x07 "
                             "-DCH32_CORECFGR=0x123703E1 -DCH32_CSR_BC1=0x01"),
    "CH32L103": dict(march="rv32imac_zicsr", mabi="ilp32", f_cpu="8000000L",
                     defines="-DCH32_MSTATUS_INIT=0x88 -DCH32_INTSYSCR_INIT=0x3 "
                             "-DCH32_CORECFGR=0x1f"),
    "CH32M030": dict(march="rv32imc_zicsr", mabi="ilp32", f_cpu="8000000L",
                     defines="-DCH32_MSTATUS_INIT=0x88 -DCH32_INTSYSCR_INIT=0x3 "
                             "-DCH32_CORECFGR=0x21 -DCH32_CSR_BC1=0x1"),
    # Excluded, same reason as tests/startup/: CH32V103 has a j-form vector
    # table and CH32H417 boots via loadcode.
}

# One board per silicon series, so the board name matches the chip marking.
# `flashable=False` means no upload backend covers it yet (probe-rs has no
# target); it still builds and guards the core against ISA/CSR regressions.
SERIES_CONFIG = {
    "CH32V003": dict(family="CH32V003", vectors="v003"),
    "CH32V002": dict(family="CH32V006", vectors="v00x"),
    "CH32V004": dict(family="CH32V006", vectors="v00x"),
    "CH32V005": dict(family="CH32V006", vectors="v00x"),
    "CH32V006": dict(family="CH32V006", vectors="v00x"),
    "CH32V007": dict(family="CH32V006", vectors="v00x"),
    "CH32M007": dict(family="CH32V006", vectors="v00x"),
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
    "CH32V205": dict(family="CH32V205", vectors="v205", flashable=False),
    "CH32V407": dict(family="CH32V407", vectors="v4x7", flashable=False),
    "CH32V467": dict(family="CH32V407", vectors="v4x7", flashable=False),
    "CH32X305": dict(family="CH32X315", vectors="x3x5", flashable=False),
    "CH32X315": dict(family="CH32X315", vectors="x3x5", flashable=False),
    "CH32M030": dict(family="CH32M030", vectors="m030", flashable=False),
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


def load_interrupts() -> dict:
    """variant tag -> ordered list of handler names (None = reserved slot)."""
    table: dict[str, list] = {}
    with open(INTERRUPTS_CSV, newline="", encoding="utf-8") as f:
        rows = [r for r in csv.DictReader(
            line for line in f if not line.startswith("#"))]
    for r in rows:
        table.setdefault(r["variant"], []).append(r["handler"] or None)
    return table


def gen_vectors(variant: str, entries: list, commit: str) -> str:
    """Emit the crt0 vector include for one startup variant."""
    out = [
        "/* DO NOT EDIT - machine generated by tools/generate/generate.py",
        f" * source: tools/generate/interrupts/interrupts.csv (variant {variant})",
        " * Interrupt vector map. Slot 0 (reset) is emitted by crt0_ch32.S;",
        " * this file starts at slot 1. Verified against the EVT startup",
        " * sources by tests/startup/ on every PR. */",
    ]
    width = max((len(h) for h in entries if h), default=0)
    for slot, handler in enumerate(entries, start=1):
        if handler is None:
            body = "    CH32_RSV"
            pad = " " * max(1, 9 + width + 1 - len(body) + 4)
            out.append(f"{body}{pad}/* {slot:3d} reserved */")
        else:
            body = f"    CH32_IRQ {handler}"
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
NON_PORT_PADS = {"ANT", "HO3", "ISP1", "LED0", "LED1",
                 "MDITP", "MDITN", "MDIRP", "MDIRN"}

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


def gen_pins(series: str, rows: list, pads: dict, adc: dict, commit: str) -> str:
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


def gen_board(series: str, rows: list, commit: str):
    """One board per series. Returns (boards.txt block, {ld name: content})."""
    cfg = SERIES_CONFIG[series]
    fam = FAMILY[cfg["family"]]
    rows = sorted(rows, key=lambda r: (int(r["flash_bytes"]), int(r["sram_bytes"]),
                                       r["part_number"]))
    board = series
    suffix = "" if cfg.get("flashable", True) else " [compile only]"

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

    for pn, label, flash, sram in entries:
        pfx = f"{board}.menu.pnum.{pn}"
        lines.append(f"{pfx}={label}")
        lines.append(f"{pfx}.build.board={pn if pn != 'ANY' else series}")
        lines.append(f"{pfx}.build.ldscript={ld_for(flash, sram)}")
        lines.append(f"{pfx}.upload.maximum_size={flash}")
        lines.append(f"{pfx}.upload.maximum_data_size={sram}")
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

    interrupts = load_interrupts()
    pads, adc, unresolved = load_pin_tables(args.tables)
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
        block, ld_files = gen_board(series, rows, commit)
        boards_blocks.append(block)
        used_variants.add(SERIES_CONFIG[series]["vectors"])
        for name, content in ld_files.items():
            outputs[args.platform / "variants" / series / name] = content
        outputs[args.platform / "variants" / series / "pins_arduino.h"] = \
            gen_pins(series, rows, pads, adc, commit)

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
            gen_vectors(variant, interrupts[variant], commit)

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
