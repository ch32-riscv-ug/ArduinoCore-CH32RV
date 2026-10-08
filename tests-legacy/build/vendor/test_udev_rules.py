"""60-ch32rv.rules at the platform root is a byte copy of the bundled ch32rv's rules.

Request B-6 (docs/ch32rv-requests.ja.md): the rules' single source is ch32rv (`doctor --emit-udev`
prints its embedded copy, the Linux release tarball bundles the same file); the platform ships a
copy for post_install.sh to install, and this test is what keeps the copy from drifting when
tools_ch32rv.json moves to a release that changed the rules.

Compared against the fetched tool (<repo>/.tools/ch32rv/<version>/, `fetch_tools.py --tool ch32rv`)
and, where that binary runs on this host, against `doctor --emit-udev` itself.
"""
import json
import os
import pathlib
import subprocess

import pytest

from loader import REPO


def bundled_version() -> str:
    return json.loads((REPO / "tools" / "index" / "tools_ch32rv.json").read_text(encoding="utf-8"))["version"]


@pytest.fixture(scope="module")
def tool_dir() -> pathlib.Path:
    d = REPO / ".tools" / "ch32rv" / bundled_version()
    if not d.is_dir():
        if os.environ.get("CH32_TESTS_REQUIRE_TOOLS"):
            pytest.fail(f"{d} is not fetched (uv run tools/index/fetch_tools.py --tool ch32rv)")
        pytest.skip(f"{d} is not fetched: uv run tools/index/fetch_tools.py --tool ch32rv")
    return d


def test_the_platform_copy_is_the_bundled_tools_rules(tool_dir):
    ours = (REPO / "60-ch32rv.rules").read_bytes()
    theirs = (tool_dir / "60-ch32rv.rules").read_bytes()
    assert ours == theirs, "60-ch32rv.rules differs from the bundled ch32rv's: copy it from .tools/ch32rv/<version>/"


def test_doctor_emits_the_same_rules(tool_dir):
    """The archive's file and the binary's embedded copy are one source; check the binary where it runs."""
    exe = tool_dir / ("ch32rv.exe" if os.name == "nt" else "ch32rv")
    if not exe.exists():
        pytest.skip(f"{exe} is not there")
    try:
        out = subprocess.run([str(exe), "doctor", "--emit-udev"], capture_output=True, timeout=30)
    except OSError as e:
        pytest.skip(f"{exe} does not run here: {e}")
    if out.returncode != 0:
        pytest.skip(f"doctor --emit-udev exited {out.returncode}: {out.stderr.decode(errors='replace')[:200]}")
    assert out.stdout == (REPO / "60-ch32rv.rules").read_bytes()


def test_post_install_is_present_and_executable():
    script = REPO / "post_install.sh"
    assert script.exists()
    assert script.read_text(encoding="utf-8").startswith("#!/bin/sh")
    if os.name != "nt":
        assert os.access(script, os.X_OK), "post_install.sh needs the executable bit"
