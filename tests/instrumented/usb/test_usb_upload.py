"""usb.upload-app-port, usb.upload-serial-port: an upload through the running sketch's own USB, as the IDE does it.

The board is flashed first through its debug probe (the runner's --flash-port) with tests/sketch_support/usb_contracts,
whose USB device carries the DFU runtime and names this build in its product string. Then arduino-cli uploads the
same image twice through the platform's recipes: once to the sketch's ch32rv-app://<USB serial> port (ch32rv_isp:
DFU_DETACH, the factory USB ISP, the write) and once to its CDC serial port (the serial recipe; ch32rv recognises a
CH32RV application's serial port and takes the same route). Each time the device must come back re-enumerated, as
this build, and the sketch must answer on its console (the probe's, which an upload over USB does not touch).
"""
import json
import pathlib
import secrets
import subprocess
import sys
import time

sys.path.insert(0, str(pathlib.Path(__file__).parent))
from usb_host import APP, PcUsbHost
import bench_ready   # staged beside the test (run_hardware.py)


def _elf_dir(case_dir):
    elves = sorted(case_dir.rglob("*.ino.elf"))
    assert elves, f"no built image under {case_dir}"
    return elves[0].parent


def test_usb_upload(dut, record_property):
    case_dir = pathlib.Path(__file__).parent
    settings = json.loads(case_dir.joinpath("case.json").read_text())
    build_id = settings["build_id"]
    serial_no = settings["target"]["dut_usb_serial"]

    m = bench_ready.ready(dut, rb" (\S+) (\d) (\S+)", nonce=secrets.token_hex(4))
    assert m.group(1).decode() == build_id
    assert m.group(2) == b"1", "USBDevice.begin() failed: " + m.group(3).decode()
    host = PcUsbHost.by_serial(serial_no)
    assert host.wait_ids(APP, 10), "application device did not enumerate"

    for route in ("app-port", "serial-port"):
        deadline = time.monotonic() + 10
        while not host.product().endswith(build_id) and time.monotonic() < deadline:
            time.sleep(0.1)
        assert host.product().endswith(build_id), "the device is not this build: " + host.product()
        port = f"ch32rv-app://{serial_no}" if route == "app-port" else host.tty()
        devnum = host.devnum()
        command = ["arduino-cli", "upload", "-p", port, "-b", settings["target"]["fqbn"],
                   "--input-dir", str(_elf_dir(case_dir)), str(case_dir)]
        done = subprocess.run(command, capture_output=True, text=True, timeout=180)
        out = done.stdout + done.stderr
        record_property(f"upload_{route}", out.strip().splitlines()[-1] if out.strip() else "")
        assert done.returncode == 0, f"upload to {port} failed:\n{out}"
        assert "verified" in out and "application started" in out, out
        assert host.wait_reenumerated(devnum, 30), f"no re-enumeration after the upload to {port}"
        assert host.wait_ids(APP, 10), f"the application did not come back after the upload to {port}"
        nonce = secrets.token_hex(4)
        m = bench_ready.ready(dut, rb" (\S+) (\d) (\S+)", nonce=nonce)
        assert m.group(1).decode() == build_id and m.group(2) == b"1", f"the sketch after the upload to {port}"
