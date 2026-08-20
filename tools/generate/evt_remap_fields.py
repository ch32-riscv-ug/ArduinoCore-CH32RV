#!/usr/bin/env -S uv run --no-project --script
# /// script
# requires-python = ">=3.10"
# ///
"""Read the AFIO remap field definitions out of EVT by running EVT's own decoder.

Each family's GPIO_PinRemapConfig() knows exactly which register bits a named
remap constant touches. Twelve families ship twelve different implementations
with nothing shared between them, so re-deriving that from the header words is
twelve chances to get it wrong. This compiles the vendor function for the host
against a two-word stub of AFIO and watches what it does instead:

    set   = the registers after calling with ENABLE from an all-zero state
    clear = the bits the DISABLE path forces to zero from an all-ones state

`clear` is the field. `set & clear`, read LSB-first over that field, is the
value. Nothing is transcribed, so nothing can be mistranscribed.

  uv run tools/generate/evt_remap_fields.py --mirrors <dir holding the CH32*
      clones> [--compare <ch32-device-data>/tables] [--json out.json]

Why this exists: ch32-device-data's remap_fields.csv gives every selector a
single `register` column, but on L103/M103 a selector spans PCFR1 *and* PCFR2,
so the field cannot be written down and `valid_values` overflows `bits`. The
data belongs upstream (docs/research/signal-name-normalization.ja.md, D-0) and
so does this tool; it sits here until they have room for it, and should be
deleted rather than kept in step once it lands there.

EVT is read in place and never copied, the same way import_vectors.py does it.
"""
from __future__ import annotations

import argparse
import collections
import csv
import json
import pathlib
import re
import subprocess
import tempfile

FN = re.compile(r"void GPIO_PinRemapConfig\(.*?\n\}", re.S)
LOCAL_DEFINE = re.compile(r"\s*#define\s+(LSB_MASK|DBGAFR_\w+|REMAP_\w+)\b")
CONST = re.compile(r"#define\s+(GPIO_(?:Remap|PartialRemap\d*|FullRemap)_(\w+))"
                   r"\s+\(\(uint32_t\)(0x[0-9A-Fa-f]+)\)")
ABS_DEREF = re.compile(r"\*\s*\(\s*(?:volatile\s+)?uint32_t\s*\*\s*\)\s*(0x[0-9A-Fa-f]+)")
SELECTOR = re.compile(r"^afio-(\w+?)-(?:remap|rm)$")
# A second constant for the same field, not a peripheral of its own: on
# V20x/V30x, GPIO_Remap_USART1_HighBit is PCFR2:26 and belongs to USART1.
SECOND_HALF = re.compile(r"^(\w+?)_HighBit$")

# One EVT per implementation family; every series inside shares the AFIO layout.
FAMILY_SERIES = {
    "CH32X035": ["CH32X035", "CH32X033"], "CH32V003": ["CH32V003"],
    "CH32M030": ["CH32M030"], "CH32V103": ["CH32V103"],
    "CH32V006": ["CH32V006", "CH32V002", "CH32V004", "CH32V005", "CH32V007",
                 "CH32M007"],
    "CH32L103": ["CH32L103", "CH32M103"], "CH32V20x": ["CH32V203", "CH32V208"],
    "CH32V307": ["CH32V307", "CH32V303", "CH32V305", "CH32V317"],
    "CH32V407": ["CH32V407", "CH32V467"], "CH32V205": ["CH32V205"],
    "CH32X315": ["CH32X315", "CH32X305"], "CH32H417": ["CH32H417"],
}

SHIM = """#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
typedef enum { DISABLE = 0, ENABLE = 1 } FunctionalState;
typedef struct { volatile uint32_t PCFR1, PCFR2; } AFIO_TypeDef;
static AFIO_TypeDef afio_inst;
#define AFIO (&afio_inst)
"""

# CH32V20x's decoder reads 0x40022030 - past the end of the documented FLASH
# block - and re-packs PCFR1 when it reads zero. Give it a page so it can, and
# probe both ways: a layout that depends on the silicon is a fact the tables
# have to carry, not a detail to average over. The default is all-ones, which
# is the plain documented layout.
DRIVER = """
static void map_probe_pages(void);
int main(int argc, char **argv) {
    map_probe_pages();
    uint32_t probe_val = argc > 1 ? (uint32_t)strtoul(argv[1], 0, 0) : 0xFFFFFFFFu;
    for (int i = 0; probe_addrs[i]; i++)
        *(volatile uint32_t *)probe_addrs[i] = probe_val;
    char line[64];
    while (fgets(line, sizeof line, stdin)) {
        uint32_t w = (uint32_t)strtoul(line, 0, 0);
        afio_inst.PCFR1 = 0; afio_inst.PCFR2 = 0;
        GPIO_PinRemapConfig(w, ENABLE);
        uint32_t s1 = afio_inst.PCFR1, s2 = afio_inst.PCFR2;
        afio_inst.PCFR1 = 0xFFFFFFFFu; afio_inst.PCFR2 = 0xFFFFFFFFu;
        GPIO_PinRemapConfig(w, DISABLE);
        printf("%08x %08x %08x %08x\\n", s1, s2,
               (uint32_t)~afio_inst.PCFR1, (uint32_t)~afio_inst.PCFR2);
    }
    return 0;
}
static void map_probe_pages(void) {
    for (int i = 0; probe_addrs[i]; i++) {
        uintptr_t page = probe_addrs[i] & ~(uintptr_t)0xFFF;
        mmap((void *)page, 0x1000, PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    }
}
"""


def family_sources(mirrors: pathlib.Path):
    """(family, *_gpio.c, *_gpio.h) for every EVT clone under `mirrors`."""
    for csrc in sorted(mirrors.glob("CH32*/EVT/**/*_gpio.c")):
        header = csrc.parent.parent / "inc" / (csrc.stem + ".h")
        if header.exists():
            yield csrc.parts[len(mirrors.parts)], csrc, header


def probe(csrc: pathlib.Path, words: list, work: pathlib.Path,
          probe_val: int = 0xFFFFFFFF):
    """Run the vendor decoder for each constant; return [(set, clear)]."""
    text = csrc.read_text(errors="ignore")
    found = FN.search(text)
    if not found:
        return None, []
    defines = "\n".join(l for l in text.splitlines() if LOCAL_DEFINE.match(l))
    addrs = sorted({int(a, 16) for a in ABS_DEREF.findall(found.group(0))})
    table = ("static const uintptr_t probe_addrs[] = {"
             + "".join(f"0x{a:x}u," for a in addrs) + "0};\n")
    prog = work / (csrc.stem + ".c")
    prog.write_text(SHIM + table + defines + "\n" + found.group(0) + DRIVER)
    exe = work / csrc.stem
    built = subprocess.run(["cc", "-w", "-o", str(exe), str(prog)],
                           capture_output=True, text=True)
    if built.returncode:
        return {"error": built.stderr.strip().splitlines()[-1:]}, addrs
    run = subprocess.run([str(exe), hex(probe_val)],
                         input="\n".join(hex(w) for w in words),
                         capture_output=True, text=True)
    if run.returncode:
        return {"error": f"exit {run.returncode}"}, addrs
    return [tuple(int(x, 16) for x in l.split())
            for l in run.stdout.splitlines()], addrs


def bit_list(pcfr1: int, pcfr2: int) -> list:
    """The field's bits, LSB first: PCFR1 ascending, then PCFR2 ascending."""
    return ([("PCFR1", b) for b in range(32) if pcfr1 >> b & 1]
            + [("PCFR2", b) for b in range(32) if pcfr2 >> b & 1])


def extract(mirrors: pathlib.Path) -> dict:
    result: dict = {}
    with tempfile.TemporaryDirectory() as tmp:
        work = pathlib.Path(tmp)
        for family, csrc, header in family_sources(mirrors):
            consts = CONST.findall(header.read_text(errors="ignore"))
            if not consts:
                continue
            words = [int(v, 16) for _, _, v in consts]
            probed, addrs = probe(csrc, words, work)
            if probed is None or isinstance(probed, dict):
                print(f"{family}: not extracted ({probed})")
                continue
            varies = False
            if addrs:
                alt, _ = probe(csrc, words, work, 0)
                varies = not isinstance(alt, dict) and alt != probed

            per = collections.defaultdict(list)
            for (name, periph, _), (s1, s2, c1, c2) in zip(consts, probed):
                merged = SECOND_HALF.match(periph)
                per[merged.group(1) if merged else periph].append(
                    (name, s1 & c1, s2 & c2, c1, c2))
            fam: dict = {}
            for periph, entries in sorted(per.items()):
                # The field is the union of what its constants clear. Usually
                # they all clear the same thing, but where WCH split a field
                # over two registers each constant clears only its own half.
                c1 = c2 = 0
                for _, _, _, e1, e2 in entries:
                    c1 |= e1
                    c2 |= e2
                bits = bit_list(c1, c2)
                fam[periph] = {
                    "bits": [f"{reg}:{bit}" for reg, bit in bits],
                    "registers": sorted({reg for reg, _ in bits}),
                    "routes": {
                        name: sum(1 << i for i, (reg, bit) in enumerate(bits)
                                  if (s1 if reg == "PCFR1" else s2) >> bit & 1)
                        for name, s1, s2, _, _ in entries},
                }
            fam["_probe"] = {"reads_hardware": [hex(a) for a in addrs],
                             "layout_depends_on_it": bool(varies)}
            result[family] = fam
            spans = sum(1 for k, v in fam.items()
                        if k != "_probe" and "PCFR2" in (v.get("registers") or []))
            note = ""
            if addrs:
                note = ("  reads " + ",".join(hex(a) for a in addrs)
                        + ("; LAYOUT VARIES" if varies else ""))
            print(f"{family:9} {len(fam) - 1:3} selectors, {spans:2} touch PCFR2{note}")
    return result


def compare(extracted: dict, tables: pathlib.Path) -> int:
    """Where remap_fields.csv and EVT disagree about a field's bits."""
    rows = {}
    with (tables / "remap_fields.csv").open(newline="", encoding="utf-8") as f:
        for r in csv.DictReader(f):
            m = SELECTOR.match(r["selector"])
            if m:
                rows[(r["series"], m.group(1).upper())] = r
    agree, differ, absent = 0, [], []
    for family, selectors in extracted.items():
        for periph, d in sorted(selectors.items()):
            if periph.startswith("_") or "bits" not in d:
                continue
            for series in FAMILY_SERIES.get(family, []):
                row = rows.get((series, periph))
                if row is None:
                    absent.append((series, periph, d["bits"]))
                    continue
                csv_bits = [f"PCFR1:{b}" for b in row["bits"].split(";") if b]
                if csv_bits == d["bits"]:
                    agree += 1
                else:
                    differ.append((series, periph, d["bits"], csv_bits))
    print(f"\nagree {agree}, differ {len(differ)}, not in the table {len(absent)}")
    for series, periph, evt, tbl in differ:
        print(f"  {series:9} {periph:12} EVT={','.join(evt):30} "
              f"table={','.join(tbl)}")
    for series, periph, evt in absent:
        print(f"  {series:9} {periph:12} not in the table (EVT={','.join(evt)})")
    return 1 if differ else 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--mirrors", type=pathlib.Path, required=True,
                    help="directory holding the CH32* EVT clones")
    ap.add_argument("--json", type=pathlib.Path)
    ap.add_argument("--compare", type=pathlib.Path,
                    help="a ch32-device-data tables/ directory to diff against")
    args = ap.parse_args()

    result = extract(args.mirrors)
    if args.json:
        args.json.write_text(json.dumps(result, indent=1), encoding="utf-8")
        print("wrote", args.json)
    if args.compare:
        return compare(result, args.compare)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
