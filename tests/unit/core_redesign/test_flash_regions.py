"""EXP-04 physical geometry, reservation accounting, and declared erase ranges."""
import importlib.util
import json
from pathlib import Path
import random
import subprocess
import sys

import pytest

REPO = Path(__file__).resolve().parents[3]
SOURCE = REPO / "tests/platform/core_redesign/flash_regions.py"
spec = importlib.util.spec_from_file_location("exp04_regions", SOURCE)
regions = importlib.util.module_from_spec(spec)
spec.loader.exec_module(regions)
CONFIG = json.loads(regions.MANIFEST.read_text())
MODES = [(t["part"], mode) for t in CONFIG["targets"] for mode in t["modes"]]


@pytest.mark.parametrize("part,mode", MODES)
@pytest.mark.parametrize("count", [0, 1, 2, 4])
def test_reservation_and_application_account_for_all_flash(part, mode, count):
    p = regions.plan(part, mode, count)
    assert p["read_unit"] == 1 and p["link_origin"] == 0 and p["physical_origin"] == 0x08000000
    assert p["app_bytes"] + p["reserved_bytes"] == p["flash_bytes"]
    assert p["reserved_bytes"] == count * p["erase_unit"]
    assert p["storage_link_end"] == p["flash_bytes"]
    assert p["storage_physical_end"] == 0x08000000 + p["flash_bytes"]
    assert p["storage_link_start"] == p["app_bytes"]
    assert p["effective_payload_bytes"] is None
    assert p["program_slots"] * p["program_unit"] == p["reserved_bytes"]
    if mode == "standard":
        assert p["program_unit"] == 2 and p["erase_unit"] == 1024
    else:
        assert p["program_unit"] == (64 if "V003" in part else 256)


@pytest.mark.parametrize("bad", [-1, True, False, 0.5, "1", None, 2**32])
def test_capacity_values_are_not_silently_rounded_or_coerced(bad):
    with pytest.raises(ValueError):
        regions.plan("CH32V003F4P6", "fast", bad)


@pytest.mark.parametrize("part,mode", MODES)
def test_boot_reservations_and_capacity_overflow(part, mode):
    g = regions.geometry(part, mode); unit = g["erase_unit"]
    p = regions.plan(part, mode, 2, unit, unit)
    assert p["app_offset"] == unit and p["storage_offset"] == g["flash_bytes"] - 3 * unit
    assert p["app_bytes"] + p["reserved_bytes"] + 2 * unit == g["flash_bytes"]
    for head, tail in [(1, 0), (0, 1), (-unit, 0), (0, True)]:
        with pytest.raises(ValueError):
            regions.plan(part, mode, 1, head, tail)
    for count in [g["flash_bytes"] // unit, g["flash_bytes"] // unit + 1, 2**31]:
        with pytest.raises(ValueError):
            regions.plan(part, mode, count)


def test_unknown_part_and_unsupported_mode_are_explicit():
    for part, mode in [("CH32V006", "fast"), ("CH32X035C8T6", "standard"), ("CH32V003F4P6", "guess")]:
        with pytest.raises(ValueError):
            regions.geometry(part, mode)


@pytest.mark.parametrize("part,mode", MODES)
def test_generated_intervals_never_overlap_or_drop_reserved_bytes(part, mode):
    rng = random.Random(0x4a11); g = regions.geometry(part, mode)
    total = g["flash_bytes"] // g["erase_unit"]
    for _ in range(100):
        head = rng.randrange(total); tail = rng.randrange(total - head)
        count = rng.randrange(total - head - tail)
        p = regions.plan(part, mode, count, head * g["erase_unit"], tail * g["erase_unit"])
        boundaries = [0, p["app_offset"], p["storage_offset"], p["storage_offset"] + p["reserved_bytes"], g["flash_bytes"]]
        assert boundaries == sorted(boundaries)
        assert sum(b - a for a, b in zip(boundaries, boundaries[1:])) == g["flash_bytes"]
        assert all(value % g["erase_unit"] == 0 for value in boundaries)


@pytest.mark.parametrize("part,mode", MODES)
def test_declared_upload_erase_ranges_include_mass_erase(part, mode):
    p = regions.plan(part, mode, 1); start = p["storage_physical_start"]; end = p["storage_physical_end"]
    assert not regions.upload_overlap(p, [(0x08000000, p["app_bytes"])])["reservation_overwritten"]
    assert not regions.upload_overlap(p, [(end, 20), (start, 0)])["reservation_overwritten"]
    assert regions.upload_overlap(p, [(start - 1, 2)])["overlapping_physical_ranges"] == [[start, start + 1]]
    assert regions.upload_overlap(p, [(start, p["reserved_bytes"])])["reservation_overwritten"]
    assert regions.upload_overlap(p, mass_erase=True)["reservation_overwritten"]
    assert not regions.upload_overlap(regions.plan(part, mode, 0), mass_erase=True)["reservation_overwritten"]
    # Link alias addresses are not automatically physical uploader addresses.
    assert not regions.upload_overlap(p, [(p["storage_link_start"], p["reserved_bytes"])])["reservation_overwritten"]
    for ranges in [[(-1, 1)], [(start, -1)], [(0xffffffff, 2)]]:
        with pytest.raises(ValueError):
            regions.upload_overlap(p, ranges)


def test_generator_creates_reviewable_files_and_refuses_overwrite(tmp_path):
    out = tmp_path / "partition"
    command = [sys.executable, str(SOURCE), "--part", "CH32V003F4P6", "--mode", "fast", "--erase-units", "1", "--out", str(out)]
    p = subprocess.run(command, capture_output=True, text=True, timeout=30)
    assert p.returncode == 0, p.stderr
    record = json.loads((out / "partition.json").read_text())
    assert record["reserved_bytes"] == 64 and record["app_bytes"] == 16320
    assert [v["reserved_bytes"] for v in json.loads((out / "menu-candidates.json").read_text())] == [0, 64, 128, 256]
    assert "NOLOAD" in (out / "probe.ld").read_text()
    before = {f.name: f.read_bytes() for f in out.iterdir()}
    assert subprocess.run(command, capture_output=True, timeout=30).returncode != 0
    assert before == {f.name: f.read_bytes() for f in out.iterdir()}
    bad = tmp_path / "bad"
    command[command.index("--erase-units") + 1] = "256"; command[-1] = str(bad)
    assert subprocess.run(command, capture_output=True, timeout=30).returncode != 0
    assert not bad.exists()
