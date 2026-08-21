"""libraries/TinyUSB/src still matches the lock.

Offline on purpose: the check hashes what is on disk, so it runs in CI without
network and catches the mistake that actually happens - a local edit to a
vendored file that nobody wrote down. A deliberate change belongs in the lock's
`patches` list, and then this test tells you it is still there.
"""
import subprocess


def test_vendored_tinyusb_matches_the_lock(repo):
    proc = subprocess.run(
        ["uv", "run", "--no-project", "python",
         "tools/vendor/vendor_tinyusb.py", "--check"],
        cwd=repo, capture_output=True, text=True)
    assert proc.returncode == 0, proc.stdout + proc.stderr
