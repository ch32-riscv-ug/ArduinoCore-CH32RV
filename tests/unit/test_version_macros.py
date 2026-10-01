"""platform.txt's compiler.version_defines say the same version as its version= line.

CH32RV_VERSION_MAJOR/MINOR/PATCH (cores/arduino/ch32rv_version.h) come from that line.
The release workflow sets version= and runs tools/index/gen_index.py, which rewrites
the line (sync_version_defines) before platform.txt is committed back; between
releases this test is what keeps a hand edit from leaving the two apart.
"""
import re

from loader import REPO


def test_version_defines_match_the_version_line():
    text = (REPO / "platform.txt").read_text(encoding="utf-8")
    version = re.search(r"^version=(\d+)\.(\d+)\.(\d+)$", text, re.M).groups()
    defines = re.search(r"^compiler\.version_defines=-DCH32RV_VERSION_MAJOR=(\d+) "
                        r"-DCH32RV_VERSION_MINOR=(\d+) -DCH32RV_VERSION_PATCH=(\d+)$", text, re.M)
    assert defines, "platform.txt has no compiler.version_defines line in the expected form"
    assert defines.groups() == version, (
        f"compiler.version_defines say {'.'.join(defines.groups())}, version= says {'.'.join(version)}: "
        f"run tools/index/gen_index.py (or fix the line by hand)")


def test_the_defines_reach_the_compiler():
    text = (REPO / "platform.txt").read_text(encoding="utf-8")
    line = re.search(r"^compiler\.defines=(.*)$", text, re.M).group(1)
    assert "{compiler.version_defines}" in line
    assert "-DCH32RV_SERIES_{build.series}" in line and " -D{build.series}" not in line
