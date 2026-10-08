"""Pure restore planning: never opens a probe or flashes a board."""
import importlib.util
import pathlib

import pytest

REPO = pathlib.Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location(
    "core_bringup", REPO / "tools/diagnostics/core-bringup/run.py")
bringup = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bringup)


@pytest.mark.parametrize("chip", tuple(bringup.GEOMETRY))
def test_restore_prefix_preserves_every_nonblank_byte(chip):
    total, writable, blank = bringup.GEOMETRY[chip]
    original = bytearray(blank * (total // len(blank)))
    original[0:4] = b"BOOT"
    original[513:516] = b"END"
    prefix = bringup.restore_prefix(bytes(original), writable, blank)
    restored = prefix + (blank * ((total - len(prefix)) // len(blank)))
    assert restored == original
    assert len(prefix) <= writable


def test_x315_nonblank_extra_region_refuses_before_flash():
    total, writable, blank = bringup.GEOMETRY["CH32X315MCU6"]
    original = bytearray(blank * (total // len(blank)))
    original[writable] ^= 1
    with pytest.raises(ValueError, match="cannot safely restore"):
        bringup.restore_prefix(bytes(original), writable, blank)


def test_incomplete_erased_word_is_rejected():
    with pytest.raises(ValueError, match="whole erased word"):
        bringup.restore_prefix(b"123", 256, b"1234")
