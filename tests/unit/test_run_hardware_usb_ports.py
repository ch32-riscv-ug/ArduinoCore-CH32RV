"""run_hardware stages the controller the record's usb names for a case (USB_TEST_CONTROLLER in build_opt.h)."""
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

import run_hardware  # noqa: E402

TARGET = dict(fqbn="ch32-riscv-ug:ch32rv:CH32H417:pnum=CH32H417QEU6", chip="CH32H417QEU6", f_cpu=400_000_000,
              upload_port="oep://e8f60ae84dc2/h417qeu6-pico")


def staged_opts(tmp_path, case, **target):
    module, _build_id, _target = run_hardware.stage_case(tmp_path, "t", case, dict(TARGET, **target))
    opt = pathlib.Path(module).parent / "build_opt.h"
    return opt.read_text() if opt.exists() else ""


def test_the_host_cases_open_the_controller_to_the_host(tmp_path):
    assert "CH32RVUsbController::SuperSpeed" in staged_opts(tmp_path, "usb", usb_host_controller="super_speed")
    assert "USB_TEST_CONTROLLER" not in staged_opts(tmp_path / "b", "usb", usb_host_controller="auto")
    assert "USB_TEST_CONTROLLER" not in staged_opts(tmp_path / "c", "usb")


def test_the_peer_host_ports_name_the_library_ports():
    """USB_HOST_PORTS builds CH32RV_USB_HOST_PORT_<KEY>; every key must be a port CH32RVUsbHost.h declares."""
    header = (pathlib.Path(__file__).resolve().parents[2] / "libraries/CH32RVUsbHost/src/CH32RVUsbHost.h").read_text()
    for name in run_hardware.USB_HOST_PORTS.values():
        assert f"    {name}," in header or f"    {name} =" in header, name
    assert run_hardware.USB_HOST_PORTS["super_speed"] == "CH32RV_USB_HOST_PORT_SUPER_SPEED"
