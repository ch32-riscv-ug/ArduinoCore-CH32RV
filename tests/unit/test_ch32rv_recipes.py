"""The command lines platform.txt hands to ch32rv, pinned word for word.

They are the contract with the bundled uploader (ch32rv's docs/cli.ja.md and its own
cli/tests/arduino_recipe.rs, which runs these arguments without a device): a flag
renamed or dropped on either side breaks every upload silently, and no build test
runs an upload. Change a line here and in platform.txt together, deliberately.

Also pinned: no menu entry passes `--chip auto` - that detects whatever is attached
and writes to it; a series ch32rv has no name for passes its series name, which
ch32rv refuses with target-not-in-db (exit 20) before opening the probe.
"""
import pathlib
import re

REPO = pathlib.Path(__file__).resolve().parents[2]


RECIPES = {
    'tools.ch32rv.upload.pattern':
        '"{path}/{cmd}" flash "{build.path}/{build.project_name}.elf" --format elf --chip {build.ch32rv_chip} --reset run --confirm-run --non-interactive --progress none {upload.verbose} {upload.erase_args} --probe "{upload.port.address}"',
    'tools.ch32rv.program.pattern':
        '"{path}/{cmd}" flash "{build.path}/{build.project_name}.elf" --format elf --chip {build.ch32rv_chip} --reset run --confirm-run --non-interactive --progress none {program.verbose} {upload.erase_args} {upload.probe_args}',
    'tools.ch32rv_isp.upload.pattern':
        '"{path}/{cmd}" --non-interactive --progress none isp flash "{build.path}/{build.project_name}.elf" --chip {build.ch32rv_chip} --entry auto {upload.erase_args} --device "{upload.port.address}" {upload.verbose}',
    'tools.ch32rv_dfu.upload.pattern':
        '"{path}/{cmd}" --non-interactive --progress none boot dfu flash "{build.path}/{build.project_name}.bin" --format bin {upload.erase_args} --device "{upload.port.address}" {upload.verbose}',
    'tools.ch32rv_hid.upload.pattern':
        '"{path}/{cmd}" --non-interactive --progress none boot hid flash "{build.path}/{build.project_name}.bin" {upload.erase_args} --device "{upload.port.address}" {upload.verbose}',
    'upload.erase_args':
        '',
    'pluggable_discovery.ch32rv.pattern':
        '"{runtime.tools.ch32rv.path}/ch32rv" arduino discovery',
    'pluggable_monitor.pattern.serial':
        '"{runtime.tools.ch32rv.path}/ch32rv" arduino monitor --protocol serial',
    'pluggable_monitor.pattern.wchlink':
        '"{runtime.tools.ch32rv.path}/ch32rv" arduino monitor --protocol wchlink',
    'pluggable_monitor.pattern.oep':
        '"{runtime.tools.ch32rv.path}/ch32rv" arduino monitor --protocol oep',
}


def platform_keys() -> dict:
    out = {}
    for line in (REPO / "platform.txt").read_text(encoding="utf-8").splitlines():
        key, sep, value = line.partition("=")
        if sep and not line.startswith("#"):
            out[key] = value
    return out


def test_the_ch32rv_command_lines_are_the_pinned_ones():
    have = platform_keys()
    for key, want in RECIPES.items():
        assert have.get(key) == want, f"{key} changed:\n  platform.txt: {have.get(key)}\n  pinned:       {want}"


def test_no_menu_entry_flashes_with_chip_auto():
    boards = (REPO / "boards.txt").read_text(encoding="utf-8")
    auto = [line for line in boards.splitlines() if re.match(r"^[^#]*\.build\.ch32rv_chip=auto$", line)]
    assert not auto, f"{len(auto)} entries flash with --chip auto, e.g. {auto[:3]}"


def test_every_flashable_board_but_uiapduino_offers_keep_store_first_and_all_flash():
    """Erase Flash: Keep Store first, passing nothing (ch32rv keeps CH32RV_FLASH_STORE's tail by default), then
    All Flash. UIAPduino uploads through its HID bootloader alone, which cannot erase the chip: no menu."""
    entries = {}
    values = {}
    for line in (REPO / "boards.txt").read_text(encoding="utf-8").splitlines():
        m = re.fullmatch(r"(\w+)\.menu\.erase\.(\w+)=(.*)", line)
        if m:
            entries.setdefault(m.group(1), []).append(m.group(2))
        m = re.fullmatch(r"(\w+)\.menu\.erase\.(\w+)\.upload\.erase_args=(.*)", line)
        if m:
            values[(m.group(1), m.group(2))] = m.group(3)
    assert entries and "UIAPDUINO_V003_V14" not in entries
    for board, keys in entries.items():
        assert keys == ["keep", "chip"], board
        assert values[(board, "keep")] == "" and values[(board, "chip")] == "--erase chip", board
