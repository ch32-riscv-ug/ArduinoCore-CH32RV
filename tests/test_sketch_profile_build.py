"""Every sketch profile builds through the package index.

This is the path the instructions tell people to use - `arduino-cli compile
--profile <name>`, which resolves the platform from the index named in
sketch.yaml. test_sketch_profiles.py covers --fqbn against the working tree and
test_package_install.py covers installing from the index; neither puts the two
together, and the profile mechanism is where they meet.

pytest-embedded's own `--run-mode build` would cover the same ground, but only
with `--profile` on the command line and only against the published index,
which does not exist yet. This serves one on loopback instead, so it runs in a
bare `pytest` and in CI with nothing to configure.
"""
import pytest

from conftest import load

pytestmark = pytest.mark.slow

harness = load("tests/sketches/profile_build.py", "profile_build")


@pytest.fixture(scope="module")
def built(repo, gcc_bin, arduino_cli, workdir):
    return harness.run(workdir / "profile-build", port=8751)


def test_every_profile_builds(built):
    """One entry per (sketch, profile), and every one of them produced an image."""
    assert built, "no sketch.yaml named a profile - the generator wrote nothing?"
    assert all(size > 0 for size in built.values())


def test_the_tier_a_boards_are_covered(built):
    """The two extremes of the range have to be in there, or this proves little."""
    profiles = {profile for _, profile in built}
    assert {"ch32x035", "ch32v003"} <= profiles, profiles
