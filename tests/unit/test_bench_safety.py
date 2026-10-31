"""Reject wrong devices and unrelated power ports before hardware operations."""
import importlib.util
import pathlib

import pytest

REPO = pathlib.Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("bench_settings", REPO / "tests/harness/bench.py")
bench = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bench)


def config(tmp_path, extra="", *, port="/dev/serial/by-id/probe"):
    file = tmp_path / "bench.toml"
    file.write_text(f'''[targets.test]
port = "{port}"
chip = "CH32V103R8T6"
fqbn = "ch32-riscv-ug:ch32rv:CH32V103:pnum=CH32V103R8T6"
f_cpu = 72000000
usb_topology = "3-2.3.1"
usb_vid = "1a86"
usb_pid = "8010"
dut_uid = "expected"
cases = ["runtime", "power"]
{extra}
''')
    return file


def test_power_cannot_turn_off_an_unrelated_bench(tmp_path):
    with pytest.raises(ValueError, match="only this target"):
        bench.load_bench(config(tmp_path, "power_ports = [1, 2]"))


def test_raw_tty_selection_is_rejected(tmp_path):
    with pytest.raises(ValueError, match="stable serial"):
        bench.load_bench(config(tmp_path, "power_ports = [1]", port="/dev/ttyACM0"))


def test_typo_is_not_ignored(tmp_path):
    with pytest.raises(ValueError, match="unknown target fields"):
        bench.load_bench(config(tmp_path, 'power_ports = [1]\ncase = ["uart"]'))


def test_upload_cannot_select_another_probe(tmp_path):
    with pytest.raises(ValueError, match="upload_port"):
        bench.load_bench(config(tmp_path, 'power_ports = [1]\nusb_serial = "our-probe"\nupload_port = "oep://another-probe/l103"'))


def test_hid_upload_must_match_the_configured_topology(tmp_path):
    file = config(tmp_path, 'power_ports = [1, 4]\nsoft_usb_topology = "3-2.3.4"\nnrst_channel = 23\nupload_port = "ch32rv-hid://3-2.3.2"')
    file.write_text(file.read_text().replace("CH32V103R8T6", "CH32V003F4U6").replace("CH32V103:pnum=CH32V003F4U6", "UIAPDUINO_V003_V14"))
    with pytest.raises(ValueError, match="upload_port"):
        bench.load_bench(file)


def test_wrong_dut_is_rejected():
    target = {"chip": "CH32V103R8T6", "dut_uid": "expected"}
    with pytest.raises(ValueError, match="UID mismatch"):
        bench.check_target(target, {"ok": True, "target": {"sku": target["chip"], "uid": "another"}})
    with pytest.raises(ValueError, match="SKU"):
        bench.check_target(target, {"ok": True, "target": {"sku": "CH32V307VCT6", "uid": "expected"}})


def test_oep_chip_id_must_match():
    target = {"chip": "CH32L103C8T6", "dut_chip_id": "0x10310710"}
    with pytest.raises(ValueError, match="chip ID mismatch"):
        bench.check_target(target, {"ok": True, "target": {"sku": target["chip"], "chip_id": "0x03500601"}})


def test_serial_path_must_reach_the_expected_usb_device(tmp_path):
    sysfs = tmp_path / "usb"
    expected, wrong = sysfs / "3-2.3.1", sysfs / "3-2.3.2"
    for device in [expected, wrong]:
        device.mkdir(parents=True)
        (device / "idVendor").write_text("1a86")
        (device / "idProduct").write_text("8010")
    port = tmp_path / "ttyACM0"
    port.touch()
    tty = tmp_path / "tty"
    (tty / port.name).mkdir(parents=True)
    interface = wrong / "interface"
    interface.mkdir()
    (tty / port.name / "device").symlink_to(interface)
    target = dict(port=str(port), usb_topology=expected.name, usb_vid="1a86", usb_pid="8010")
    with pytest.raises(ValueError, match="different USB topology"):
        bench.check_identity(target, sysfs=sysfs, tty_root=tty)


def test_oep_identity_mismatch_stops_target_operations():
    target = dict(usb_vid='1a86', usb_pid='7523', probe_unit_id='0070070d9394')
    for snapshot in ({}, dict(unit_id='another-probe')):
        with pytest.raises(ValueError, match='unit ID mismatch'):
            bench.check_probe(target, snapshot)
    bench.check_probe(target, dict(unit_id='0070070D9394', firmware=None))


def test_the_bench_takes_no_shared_lock(tmp_path):
    source = config(tmp_path, 'power_ports = [1]')
    source.write_text('lock_file = "bench.lock"\n' + source.read_text())
    with pytest.raises(ValueError, match="only targets"):
        bench.load_bench(source)
    assert not hasattr(bench, "shared_lock")


def usb_config(tmp_path, topology):
    file = config(tmp_path, f'dut_usb_topology = "{topology}"')
    file.write_text(file.read_text().replace('cases = ["runtime", "power"]', 'cases = ["usb"]')
                    .replace("CH32V103:pnum=CH32V103R8T6", "CH32X035:pnum=CH32X035F8U6").replace("CH32V103R8T6", "CH32X035F8U6"))
    return file


def test_usb_needs_the_dut_port_not_the_probe(tmp_path):
    with pytest.raises(ValueError, match="dut_usb_topology"):
        bench.load_bench(usb_config(tmp_path, "3-2.3.1"))
    assert bench.load_bench(usb_config(tmp_path, "3-2.3.2"))["targets"]["test"]["dut_usb_topology"] == "3-2.3.2"


def test_usb_needs_a_supported_series(tmp_path):
    file = usb_config(tmp_path, "3-2.3.2")
    file.write_text(file.read_text().replace("CH32X035:pnum=CH32X035F8U6", "CH32V103:pnum=CH32V103R8T6").replace("CH32X035F8U6", "CH32V103R8T6"))
    with pytest.raises(ValueError, match="series"):
        bench.load_bench(file)
