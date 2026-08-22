"""The test tree obeys its own layout rules.

Two mistakes are easy to make here and neither shows up as a failure: a check
put somewhere `pytest` does not look, and a manual test given a name that makes
a bare `pytest` flash the attached board. Both look like a green run - the
first because the check silently never ran, the second because it only bites
the person who happens to have hardware plugged in.

So the rules from tests/TEST_PLAN.ja.md are asserted rather than written down
and hoped for:

  - the root holds documents and configuration, nothing executable
  - every automated entry point is test_<something>.py inside a category
    directory, which is what a bare `pytest` collects
  - every manual entry point is <case>/<case>.py with no test_ prefix, which is
    what a bare `pytest` must *not* collect
  - no two test modules share a file name
"""
import pathlib

import pytest

TESTS = pathlib.Path(__file__).resolve().parents[1]

# What may sit directly in tests/. Everything else belongs to a category.
ROOT_ALLOWED = {
    "README.ja.md", "README.md",
    "TEST_PLAN.ja.md", "TEST_PLAN.md",
    "conftest.py",                   # kept small; see its docstring
    "loader.py",                     # what conftest.py is deliberately not
    "pyproject.toml", "uv.lock",
    ".env.example", ".env",          # .env is this bench's, and is not committed
}

# Category directories, and what each one is for. A new kind of check adds a
# row here and a directory; anything not listed is a file in the wrong place.
CATEGORIES = {
    "generated": "generated files still match the device-data tables",
    "vendor": "vendored snapshots still match their locks",
    "startup": "crt0 and vector tables against the EVT sources",
    "compile": "the compile sweeps and the size baseline",
    "sizebench": "newlib size measurement",
    "package": "the Board Manager distribution installs and builds",
    "sketches": "per-sketch API tests, and the profile builds beside them",
    "unit": "small checks that need neither a board nor a build",
    "manual": "needs the bench: real hardware and, sometimes, a person",
}

NOT_COLLECTED = {"manual"}       # excluded in pyproject.toml norecursedirs


def _ignored(path: pathlib.Path) -> bool:
    parts = set(path.parts)
    return bool(parts & {".venv", ".pytest_cache", "__pycache__", ".git"})


def test_the_root_holds_no_tests_and_no_harnesses():
    """No entry points and no harnesses at the root; those live in a category.

    The two modules that are allowed are the shared ones, and they are listed
    by name rather than by pattern so that adding a third is a decision.
    """
    stray = sorted(p.name for p in TESTS.iterdir()
                   if p.is_file() and p.name not in ROOT_ALLOWED)
    assert not stray, (f"put these in a category directory: {stray}  "
                       f"(allowed at the root: {sorted(ROOT_ALLOWED)})")


def test_every_category_directory_is_declared():
    """A directory nobody documented is a category nobody will look in."""
    dirs = sorted(p.name for p in TESTS.iterdir()
                  if p.is_dir() and not _ignored(p))
    assert set(dirs) <= set(CATEGORIES), (
        f"undeclared: {sorted(set(dirs) - set(CATEGORIES))}. Add it to "
        f"CATEGORIES here and to the table in TEST_PLAN.ja.md, or move the "
        f"files into an existing category.")


@pytest.mark.parametrize("name", sorted(set(CATEGORIES) - NOT_COLLECTED))
def test_every_collected_category_has_an_entry_point(name):
    """Declared but empty means the category was renamed and half-updated."""
    directory = TESTS / name
    if not directory.is_dir():
        pytest.skip(f"{name}/ does not exist yet")
    assert list(directory.rglob("test_*.py")), \
        f"{name}/ has no test_*.py, so a bare `pytest` runs nothing in it"


def test_manual_tests_are_never_collected_by_accident():
    """The prefix is the safety catch, and norecursedirs is the second one.

    Both are needed: the prefix keeps `pytest manual/` from flashing a board
    just because someone pointed pytest at the directory, and norecursedirs
    keeps a bare `pytest` out even if a file is one day misnamed.
    """
    prefixed = sorted(str(p.relative_to(TESTS))
                      for p in (TESTS / "manual").rglob("test_*.py"))
    assert not prefixed, (
        "these would be collected the moment manual/ is pointed at directly; "
        f"rename them to <case>.py: {prefixed}")


def test_every_manual_case_has_its_entry_point():
    """manual/<case>/<case>.py, so naming the directory is enough to find it."""
    missing = []
    for case in sorted((TESTS / "manual").iterdir()):
        if not case.is_dir() or _ignored(case):
            continue
        if not (case / f"{case.name}.py").exists():
            missing.append(case.name)
    assert not missing, f"manual/<case>/<case>.py is missing for {missing}"


def test_only_the_root_conftest_is_collected():
    """A second collected conftest.py silently replaces the first.

    pytest imports every conftest.py under the plain module name `conftest`, so
    two of them in the collected tree means `sys.modules["conftest"]` is
    whichever loaded last, and anything importing from it gets the wrong file
    with an ImportError that names neither as the cause.

    manual/ keeps its own, because it is in norecursedirs and is loaded only
    when one of its files is named on the command line. That is still enough to
    shadow the root one in a command that names both, which is why nothing
    imports from conftest at all - shared code lives in loader.py.
    """
    collected = [p for p in TESTS.rglob("conftest.py")
                 if not _ignored(p) and "manual" not in p.relative_to(TESTS).parts]
    assert collected == [TESTS / "conftest.py"], \
        f"only tests/conftest.py may be collected; also found: {collected}"


def test_no_two_test_modules_share_a_name():
    """pytest imports them into a flat namespace, so a clash is an import error.

    It surfaces as "import file mismatch" pointing at a stale .pyc, which is a
    confusing way to learn that two directories both have a test_serial.py.
    """
    seen = {}
    clashes = []
    for path in sorted(TESTS.rglob("test_*.py")):
        if _ignored(path):
            continue
        first = seen.setdefault(path.name, path)
        if first != path:
            clashes.append(f"{first.relative_to(TESTS)} / {path.relative_to(TESTS)}")
    assert not clashes, f"same file name in two places: {clashes}"


def test_the_plan_lists_every_entry_point():
    """The table in TEST_PLAN is the map; a check missing from it is invisible.

    Only the category-level entry points are checked. The per-sketch files are
    covered by their own row in the plan ("1 case = 1 directory") rather than
    one line each.
    """
    plan = (TESTS / "TEST_PLAN.ja.md").read_text(encoding="utf-8")
    missing = []
    for name in sorted(set(CATEGORIES) - NOT_COLLECTED):
        for path in sorted((TESTS / name).glob("test_*.py")):
            if path.name not in plan:
                missing.append(str(path.relative_to(TESTS)))
    assert not missing, f"not mentioned in TEST_PLAN.ja.md: {missing}"
