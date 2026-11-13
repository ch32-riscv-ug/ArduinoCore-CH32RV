"""tests/hw: every sketch.yaml carries the profiles of tests/hw/profiles.toml (harness/profiles.py writes them),
every combination names a profile, and the collection-time judgement (place.static_reason) reads the part alone."""
import pathlib
import sys
import tomllib

REPO = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tests"))

from harness import place, profiles  # noqa: E402


def test_sketch_yaml_files_are_current():
    assert profiles.main(["--check"]) == 0, "run: uv run --project tests --locked python -m harness.profiles (in tests/)"


def test_every_combination_names_a_profile():
    known = set(profiles.load())
    combos = tomllib.loads((REPO / "tests/pyproject.toml").read_text())["tool"]["pytest"]["ini_options"]["arduino_combos"]
    for line in combos:
        name, _, spec = line.partition(":")
        for role in spec.split(","):
            key, _, value = role.strip().partition("=")
            if key == "primary":
                assert value in known, f"{name}: primary {value} is not in tests/hw/profiles.toml"


def test_static_reason_reads_the_part_alone():
    assert place.static_reason("gpio", "CH32X035C8T6") is None
    assert place.static_reason("eth", "CH32V203RBT6") is None
    assert place.static_reason("eth", "CH32V203C8T6") == "CH32V203 has no eth"
    assert place.static_reason("eth", "CH32V208CBU6") is not None
    assert place.static_reason("spi", "CH32M030G8R7") == "CH32M030G8R7 has no spi"
