"""The EXTI lines that are not pins (cores/arduino/internal/ch32rv_exti_line.h, written by hand) against
ch32-device-data's index/exti_lines.csv: the same line numbers for the same sources in every series.

Kept by hand rather than generated (2026-11-20): device-data names five families' sources and vectors otherwise
(USBHS1 / USBPD0 / USB / USBFSOTG / LPTIM1 / OSCCAL, the vectors' case), V103's line 18 is CH32F103's USBD, and V307's
line 20 differs by die - a generated table would carry these as an alias layer of its own. This test is the check the
header's comment promises, run every time."""
import csv
import pathlib
import re

import pytest

REPO = pathlib.Path(__file__).resolve().parents[2]
HEADER = REPO / "cores/arduino/internal/ch32rv_exti_line.h"
TABLE = REPO / ".tools/ch32-device-data/index/exti_lines.csv"

# The header's series blocks -> device-data's family (the first series named in each #if / #elif).
FAMILY = {"CH32V003": "CH32V003", "CH32V103": "CH32V103", "CH32V203": "CH32V20x", "CH32V303": "CH32V307",
          "CH32V205": "CH32V205", "CH32V407": "CH32V407", "CH32L103": "CH32L103", "CH32X035": "CH32X035",
          "CH32M030": "CH32M030", "CH32X305": "CH32X315", "CH32H415": "CH32H417"}
# device-data's source -> the core's CH32RV_EXTI_<name>.
ALIAS = {"USBHS1_WAKEUP": "USBHS_WAKEUP", "USBPD0_WAKEUP": "USBPD_WAKEUP", "USB_WAKEUP": "USBFS_WAKEUP",
         "USBFSOTG_WAKEUP": "USBFS_WAKEUP", "USBD_WAKEUP;USBFSOTG_WAKEUP": "USBFS_WAKEUP",
         "LPTIM1_WAKEUP": "LPTIM_WAKEUP", "OSCCAL_WAKEUP": "OSC32K_WAKEUP"}
# Not attachExtiLine()'s: pins, the AWU (lightSleep()'s), the RTC alarm (ch32rv_rtc_alarm.c's, CH32RV_EXTI_RTC_ALARM).
SKIP = {"GPIO", "AWU", "PC18", "PC19", "RTC_ALARM"}
# (family, line): rows the core leaves out on purpose (the header says why).
LEFT_OUT = {("CH32V103", 18)}       # CH32F103's USBD; V103's USB wake-up is line 19


def header_blocks():
    text = HEADER.read_text(encoding="utf-8")
    blocks, current = {}, None
    for line in text.splitlines():
        m = re.match(r"#(?:el)?if defined\(CH32RV_SERIES_(\w+)\)", line)
        if m:
            current = blocks.setdefault(FAMILY[m.group(1)], {})
            continue
        m = re.match(r"#define CH32RV_EXTI_(\w+) (\d+)u$", line)
        if m and current is not None:
            current[int(m.group(2))] = m.group(1)
        if line.startswith("/* The RTC alarm's line"):
            current = None
    return blocks


def table_rows():
    if not TABLE.exists():
        pytest.skip("no ch32-device-data checkout (.tools/ch32-device-data)")
    rows = {}
    with TABLE.open(encoding="utf-8") as f:
        for r in csv.DictReader(f):
            if r["source"] in SKIP or (r["family"], int(r["line"])) in LEFT_OUT:
                continue
            if r["family"] == "CH32V307" and r["line"] == "20" and not r["vector"]:
                continue                     # the D8 die's USBFS row: no vector (the core takes D8C's USBHS)
            rows.setdefault(r["family"], {})[int(r["line"])] = ALIAS.get(r["source"], r["source"])
    return rows


def test_the_header_names_the_lines_device_data_has():
    blocks, rows = header_blocks(), table_rows()
    assert set(blocks) == set(FAMILY.values())
    for family, lines in blocks.items():
        if family == "CH32V003":
            # the block covers V00x too: device-data's CH32V006 has the same line
            assert rows.get("CH32V006") == rows.get("CH32V003"), "V003 and V00x differ in device-data"
        assert lines == rows.get(family, {}), f"{family}: header {lines}, device-data {rows.get(family)}"
