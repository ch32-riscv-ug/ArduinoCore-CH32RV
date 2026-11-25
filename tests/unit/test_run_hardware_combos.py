"""run_hardware --combo: the addresses of arduino_combos' primary profiles, from TEST_SERIAL_PORT_<PROFILE>."""
import pathlib
import sys

import pytest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import run_hardware  # noqa: E402


def write(tmp_path, combos, env):
    pyproject = tmp_path / "pyproject.toml"
    pyproject.write_text("[tool.pytest.ini_options]\narduino_combos = [\n"
                         + "".join(f'  "{c}",\n' for c in combos) + "]\n")
    env_file = tmp_path / ".env"
    env_file.write_text(env)
    return pyproject, env_file


def test_all_and_one_name(tmp_path, monkeypatch):
    monkeypatch.delenv("TEST_SERIAL_PORT_X035C8T6_S3", raising=False)
    monkeypatch.delenv("TEST_SERIAL_PORT_M030G8R7", raising=False)
    pyproject, env_file = write(tmp_path, ["x035c8t6-s3: primary=x035c8t6-s3", "m030g8r7: primary=m030g8r7"],
                                "TEST_SERIAL_PORT_X035C8T6_S3=oep://80f1b2d835d6/x035c8t6-s3\n"
                                "TEST_SERIAL_PORT_M030G8R7='wchlink://F90E8F067DFD'\n")
    assert run_hardware.combo_addresses(["all"], pyproject, env_file) == [
        "oep://80f1b2d835d6/x035c8t6-s3", "wchlink://F90E8F067DFD"]
    assert run_hardware.combo_addresses(["m030g8r7"], pyproject, env_file) == ["wchlink://F90E8F067DFD"]


def test_an_unknown_name_or_a_missing_port_is_refused(tmp_path, monkeypatch):
    monkeypatch.delenv("TEST_SERIAL_PORT_V003F4P6", raising=False)
    pyproject, env_file = write(tmp_path, ["v003f4p6: primary=v003f4p6"], "")
    with pytest.raises(ValueError, match="no combination nope"):
        run_hardware.combo_addresses(["nope"], pyproject, env_file)
    with pytest.raises(ValueError, match="no TEST_SERIAL_PORT_V003F4P6"):
        run_hardware.combo_addresses(["all"], pyproject, env_file)


def test_the_repository_combos_are_the_profiles():
    """Every combination's primary is a profile of tests/hw/profiles.toml, and every profile has a combination."""
    import tomllib
    tests = pathlib.Path(run_hardware.__file__).parent
    combos = tomllib.loads((tests / "pyproject.toml").read_text())["tool"]["pytest"]["ini_options"]["arduino_combos"]
    primaries = {line.split("primary=")[1].split(",")[0].strip() for line in combos}
    profiles = set(tomllib.loads((tests / "hw/profiles.toml").read_text()))
    assert primaries == profiles
