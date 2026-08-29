"""The startup harness tests the parameters boards.txt actually ships.

tests/startup proves the unified crt0 lands the machine in the same state as
WCH's own startup code, per family. It does that with its own table of
-march/-mabi and CH32_MSTATUS_INIT / CH32_INTSYSCR_INIT / CH32_CORECFGR
values - and boards.txt carries the same numbers, generated from device-data.

Two copies of the same parameter is the setup for a silent pass: change the
generator, and the equivalence proof keeps passing against values no board
selects any more. Nothing would say so, because both sides are internally
consistent.

So the harness table is checked against the generated boards.txt here, which
costs no board and no build. The harness stays the source for *what to build*;
boards.txt stays the source for *what the values are*.
"""
import pathlib
import re

import pytest

from loader import load

REPO = pathlib.Path(__file__).resolve().parents[2]
harness = load("tests/startup/startup_equivalence.py", "startup_equivalence")

BOARDS_TXT = (REPO / "boards.txt").read_text(encoding="utf-8")


def _field(board: str, key: str):
    m = re.search(rf"^{re.escape(board)}\.build\.{key}=(.*)$", BOARDS_TXT, re.M)
    return m.group(1).strip() if m else None


def _boards_by_tag() -> dict:
    """vector-table tag -> the boards built with it.

    build.vector_variant is the same vocabulary as the harness's Family.tag,
    which is what lets the two tables be compared at all.
    """
    out: dict = {}
    for board in re.findall(r"^([A-Za-z0-9_]+)\.name=", BOARDS_TXT, re.M):
        out.setdefault(_field(board, "vector_variant"), []).append(board)
    return out


# A harness family with no board is not an error - it is a die variant nobody
# selects yet - but it is also not something to discover by accident, so the
# set is pinned. CH32V203 is built as v20x_d6 and CH32V208 as v20x_d8w, which
# leaves the plain D8 startup covered by the equivalence proof and by nothing
# else.
FAMILIES_WITHOUT_A_BOARD = {"v20x_d8"}


def test_every_harness_family_is_accounted_for():
    tags = {fam.tag for fam in harness.FAMILIES}
    have = {t for t in _boards_by_tag() if t}
    orphans = tags - have
    assert orphans == FAMILIES_WITHOUT_A_BOARD, (
        f"harness families with no board changed: {sorted(orphans)} "
        f"(expected {sorted(FAMILIES_WITHOUT_A_BOARD)})")


def test_every_board_tag_has_a_harness_family():
    """The other direction: a board whose startup nothing proves equivalent."""
    tags = {fam.tag for fam in harness.FAMILIES}
    unproven = {t: b for t, b in _boards_by_tag().items() if t and t not in tags}
    assert not unproven, (
        f"these boards select a vector variant the startup harness does not "
        f"build: {unproven}")


@pytest.mark.parametrize("fam", harness.FAMILIES, ids=lambda f: f.tag)
def test_the_isa_matches_boards_txt(fam):
    for board in _boards_by_tag().get(fam.tag, []):
        assert (_field(board, "march"), _field(board, "mabi")) == \
               (fam.march, fam.mabi), (
            f"{board} builds -march={_field(board, 'march')} "
            f"-mabi={_field(board, 'mabi')}, but the startup equivalence proof "
            f"for {fam.tag} uses -march={fam.march} -mabi={fam.mabi}")


@pytest.mark.parametrize("fam", harness.FAMILIES, ids=lambda f: f.tag)
def test_the_startup_defines_match_boards_txt(fam):
    for board in _boards_by_tag().get(fam.tag, []):
        shipped = set((_field(board, "startup_defines") or "").split())
        assert shipped == set(fam.defines), (
            f"{board} ships {sorted(shipped)} but the startup equivalence "
            f"proof for {fam.tag} uses {sorted(fam.defines)}")
