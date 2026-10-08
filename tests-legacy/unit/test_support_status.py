"""docs/support-status.ja.md says per series how far it is checked; the board names no longer do. These keep the
table honest against what decides each state: a bench file under tests-legacy/benches (on hardware), the bundled ch32rv's
chip list (flashable), boards.txt (the series there are)."""

import pathlib
import re
import tomllib

REPO = pathlib.Path(__file__).resolve().parents[2]
DOC = (REPO / "docs/support-status.ja.md").read_text(encoding="utf-8")
BOARDS = (REPO / "boards.txt").read_text(encoding="utf-8")
CHIPS = (REPO / "tools/index/ch32rv_chips.csv").read_text(encoding="utf-8")

ON_HARDWARE, FLASHABLE, BUILD_ONLY = "実機で確認", "書き込み可・未確認", "ビルドのみ"


def series_rows() -> dict[str, tuple[str, str]]:
    section = DOC.split("## 系列ボード", 1)[1].split("\n## ", 1)[0]
    return {m.group(1): (m.group(2).strip(), m.group(3))
            for m in re.finditer(r"^\| (CH32\w+) \| ([^|]+) \|([^\n]*)\|$", section, re.M)}


def benches() -> list[dict]:
    """Each bench file's [dut]: board (the series) and part."""
    return [tomllib.loads(p.read_text(encoding="utf-8"))["dut"] for p in sorted((REPO / "tests-legacy/benches").glob("*.toml"))]


def test_every_series_board_has_a_row():
    generic = set(re.findall(r"^(CH32\w+)\.name=Generic ", BOARDS, re.M))
    assert set(series_rows()) == generic


def test_no_state_in_board_names():
    assert "[compile only]" not in BOARDS


def test_build_only_is_exactly_what_ch32rv_does_not_know():
    known = {line.split(",")[2] for line in CHIPS.splitlines()
             if line and not line.startswith("#") and not line.startswith("ch32rv_version")}
    for series, (state, _) in series_rows().items():
        assert (state == BUILD_ONLY) == (series not in known), f"{series}: {state}"


def test_on_hardware_is_exactly_the_series_with_a_bench():
    benched = {b["board"] for b in benches()}
    for series, (state, _) in series_rows().items():
        assert (state == ON_HARDWARE) == (series in benched), f"{series}: {state}"
        assert state in (ON_HARDWARE, FLASHABLE, BUILD_ONLY), f"{series}: unknown state {state!r}"


def test_each_bench_part_is_named():
    rows = series_rows()
    for b in benches():
        assert b["part"] in rows[b["board"]][1], f"{b['part']} is on a bench but not in the {b['board']} row"
