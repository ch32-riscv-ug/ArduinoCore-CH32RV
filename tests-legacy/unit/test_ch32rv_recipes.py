"""The command lines platform.txt hands to ch32rv, pinned word for word.

They are the contract with the bundled uploader (ch32rv's docs/cli.ja.md and its own
cli/tests-legacy/arduino_recipe.rs, which runs these arguments without a device): a flag
renamed or dropped on either side breaks every upload silently, and no build test
runs an upload. Change a line here and in platform.txt together, deliberately.

Also pinned: no menu entry passes `--chip auto` - that detects whatever is attached
and writes to it; a series ch32rv has no name for passes its series name, which
ch32rv refuses with target-not-in-db (exit 20) before opening the probe.
"""
import re

from loader import REPO

RECIPES = {
    'tools.ch32rv.upload.pattern':
        '"{path}/{cmd}" flash "{build.path}/{build.project_name}.elf" --format elf --chip {build.ch32rv_chip} --reset run --confirm-run --non-interactive --progress none {upload.verbose} --probe "port:{upload.port.address}"',
    'tools.ch32rv.program.pattern':
        '"{path}/{cmd}" flash "{build.path}/{build.project_name}.elf" --format elf --chip {build.ch32rv_chip} --reset run --confirm-run --non-interactive --progress none {program.verbose} {upload.probe_args}',
    'tools.ch32rv_hid.upload.pattern':
        '"{path}/{cmd}" --non-interactive --progress none boot hid flash "{build.path}/{build.project_name}.bin" --probe "port:{upload.port.address}" {upload.verbose}',
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


def test_series_ch32rv_does_not_know_pass_their_series_name():
    """Such a board's upload stops with ch32rv's target-not-in-db (exit 20) instead of guessing a chip."""
    boards = (REPO / "boards.txt").read_text(encoding="utf-8")
    chips_csv = (REPO / "tools/index/ch32rv_chips.csv").read_text(encoding="utf-8")
    known = {line.split(",")[2] for line in chips_csv.splitlines()
             if line and not line.startswith("#") and not line.startswith("ch32rv_version")}
    unknown = [b for b in re.findall(r"^(\w+)\.name=Generic ", boards, re.M)
               if re.search(rf"^{b}\.build\.series=(\S+)$", boards, re.M).group(1) not in known]
    assert unknown, "every series is known to ch32rv: this test has nothing left to check"
    for board in unknown:
        series = re.search(rf"^{board}\.build\.series=(\S+)$", boards, re.M).group(1)
        chips = set(re.findall(rf"^{board}\.menu\.pnum\.\w+\.build\.ch32rv_chip=(\S+)$", boards, re.M))
        assert chips == {series}, f"{board} passes {chips}, not its series name {series}"
