"""sd.spi / sd.sdio (case sd): an SD card on the bench, through the SD library (SPI) and SD_MMC (SDIO), on the pads
the probe labels (bench_lines.py: spisd_sck / _miso / _mosi / _cs; sdio_ck / _cmd / _d0, and _d1-_d3 for 4 lines).
Each test skips when its labels are not there. The card must hold a FAT volume: the test identifies the card,
mounts it, writes a 64 KB file, reads it back, lists the root and removes the file. The probe's channels on those
lines stay as they are (inputs): they only name the wiring."""
import json
import pathlib
import secrets

import pytest

import sys

sys.path.insert(0, str(pathlib.Path(__file__).parent))   # bench_lines.py is staged beside the test
from bench_lines import require

CARD_TYPES = {1: "MMC", 2: "SDSC", 3: "SDHC/SDXC"}


def settings(test_file):
    return json.loads(pathlib.Path(test_file).with_name("case.json").read_text())


def ask(dut, line, reply, timeout=10):
    dut.write(line + "\n")
    return dut.expect(reply + rb"\r?\n", timeout=timeout)


def ready(dut, case):
    nonce = secrets.token_hex(4)
    m = ask(dut, f"ready {nonce}", rb"READY " + nonce.encode() + rb" (\S+)", timeout=15)
    assert m.group(1).decode() == case["build_id"], "the DUT runs another build"


def host_of(request):
    try:
        return request.getfixturevalue("oep_host")
    except pytest.FixtureLookupError:
        return None


def files(dut, record_property, label):
    m = ask(dut, "files 64", rb"FILES (\d) (\d+) (\d+) (\d+)", timeout=60)
    assert m.group(1) == b"1", "write, read back or remove failed"
    record_property(f"{label}_write_ms_64k", int(m.group(2)))
    record_property(f"{label}_read_ms_64k", int(m.group(3)))
    assert int(m.group(4)) >= 1
    ask(dut, "end", rb"END")


def test_sd_spi(dut, request, record_property):
    case = settings(__file__)
    pads = require(host_of(request), case["target"], "spisd")
    ready(dut, case)
    p = {role: line["pin"] for role, line in pads.items()}
    m = ask(dut, f"spi {p['spisd_sck']} {p['spisd_miso']} {p['spisd_mosi']} {p['spisd_cs']} 4000000",
            rb"SPI (\d) (\d) (\d+) (\d)", timeout=20)
    assert m.group(1) == b"1", "no card answered, or it holds no FAT volume"
    record_property("spisd_card", CARD_TYPES.get(int(m.group(2)), m.group(2).decode()))
    assert int(m.group(3)) > 0 and int(m.group(4)) > 0
    files(dut, record_property, "spisd")


def test_sd_sdio(dut, request, record_property):
    case = settings(__file__)
    pads = require(host_of(request), case["target"], "sdio")
    ready(dut, case)
    p = {role: line["pin"] for role, line in pads.items()}
    wide = all(f"sdio_d{i}" in p for i in (1, 2, 3))
    pins = [p["sdio_ck"], p["sdio_cmd"], p["sdio_d0"]] + ([p["sdio_d1"], p["sdio_d2"], p["sdio_d3"]] if wide else [])
    m = ask(dut, "sdio " + " ".join(map(str, pins)) + " 20000", rb"SDIO (\d) (\d) (\d+) (\d) (\d+) (\d)", timeout=20)
    assert m.group(1) == b"1", "no card answered, the pads are not the series' SDIO pins, or no FAT volume"
    record_property("sdio_card", CARD_TYPES.get(int(m.group(2)), m.group(2).decode()))
    record_property("sdio_clock_khz", int(m.group(5)))
    assert int(m.group(6)) == (4 if wide else 1)
    files(dut, record_property, "sdio")
