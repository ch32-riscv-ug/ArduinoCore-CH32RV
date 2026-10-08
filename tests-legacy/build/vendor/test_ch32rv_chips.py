"""tools/index/ch32rv_chips.csv is the bundled ch32rv's own chip list.

The generator takes every `--chip` value from that file (tools/generate/generate.py
ch32rv_chip()): the exact part where ch32rv knows it, else the family, else the series
name that ch32rv refuses. The file is rebuilt by hand from `ch32rv db list --json`, so
this compares it with the binary tools_ch32rv.json bundles - a ch32rv release that adds
or drops a chip fails here instead of shipping boards.txt entries ch32rv disagrees with.
Compared: family, sku (the csv's `chip`) and `verified`; the csv's series column is the
core's own grouping and is checked against the family only.
"""
import csv
import json
import os
import subprocess

import pytest

from loader import REPO


def bundled_tool():
    version = json.loads((REPO / "tools" / "index" / "tools_ch32rv.json").read_text(encoding="utf-8"))["version"]
    exe = REPO / ".tools" / "ch32rv" / version / ("ch32rv.exe" if os.name == "nt" else "ch32rv")
    if not exe.exists():
        if os.environ.get("CH32_TESTS_REQUIRE_TOOLS"):
            pytest.fail(f"{exe} is not fetched (uv run tools/index/fetch_tools.py --tool ch32rv)")
        pytest.skip(f"{exe} is not fetched: uv run tools/index/fetch_tools.py --tool ch32rv")
    return exe


def test_the_chip_list_is_the_bundled_ch32rvs():
    exe = bundled_tool()
    try:
        out = subprocess.run([str(exe), "db", "list", "--json"], capture_output=True, text=True, timeout=60)
    except OSError as e:
        pytest.skip(f"{exe} does not run here: {e}")
    skus = json.loads(out.stdout)["result"]["skus"]
    theirs = {(s["family"], s["sku"], str(s["verified"]).lower()) for s in skus}
    with open(REPO / "tools" / "index" / "ch32rv_chips.csv", newline="", encoding="utf-8") as f:
        rows = list(csv.DictReader(line for line in f if not line.startswith("#")))
    ours = {(r["family"], r["chip"], r["verified"]) for r in rows}
    assert ours == theirs, (
        f"ch32rv_chips.csv differs from `{exe.name} db list --json`: "
        f"only in the csv {sorted(ours - theirs)[:8]}, only in ch32rv {sorted(theirs - ours)[:8]} - "
        f"rebuild the csv from the bundled ch32rv")
