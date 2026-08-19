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


def gen_pins_stub(cfg: dict, max_gpio: int, commit: str) -> str:
    """Placeholder variant header. Real pin maps come from ch32-device-data
    (pins/pin_functions) once the Arduino pin numbering is agreed (Q-011)."""
    return (
        "/* DO NOT EDIT - machine generated by tools/generate/generate.py\n"
        f" * source: ch32-device-data tables @ git {commit}\n"
        " *\n"
        " * PLACEHOLDER. This is not a pin map: no pad is mapped to an Arduino\n"
        " * pin number yet. Generated from device-data only so the variant\n"
        " * compiles. Replaced once Q-011 fixes the pin numbering scheme. */\n"
        "#pragma once\n\n"
        f"#define CH32_VARIANT_{cfg['variant']} 1\n"
        f"#define NUM_DIGITAL_PINS {max_gpio}\n"
        "#define LED_BUILTIN 0\n")


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
        max_gpio = max((int(r["gpio_count"]) for r in rows if r["gpio_count"]), default=0)
        outputs[args.platform / "variants" / series / "pins_arduino.h"] = \
            gen_pins_stub({"variant": series}, max_gpio, commit)

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
