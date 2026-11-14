"""sd.spi / sd.sdio (case sd): an SD card on the bench, through the SD library (SPI) and SD_MMC (SDIO), on the pads
the probe labels (bench_lines.py: spisd_sck / _miso / _mosi / _cs; sdio_ck / _cmd / _d0, and _d1-_d3 for 4 lines),
else on the pads the core record's parts.sd names (a board with the slot and no probe lines, behind a WCH-Link:
bus spi for sd.spi, sd1 / sd4 for sd.sdio). Each test skips when neither names its pads. The card must hold a FAT volume: the test identifies the card,
mounts it, writes a 64 KB file, reads it back, lists the root and removes the file. The probe's channels on those
lines stay as they are (inputs): they only name the wiring. A card keeps to the bus mode it answered in until its
power goes, so an SDIO slot behind a WCH-Link gets the Link's USB port powered off and on before each test
(fresh_card)."""
import json
import pathlib
import secrets
import subprocess
import time

import pytest

import sys

sys.path.insert(0, str(pathlib.Path(__file__).parent))   # bench_lines.py is staged beside the test
from bench_lines import lines, pin_number

# parts.sd's members (tests/RECORDS.ja.md §4.4) as the labels' roles.
RECORD_ROLES = {"spisd": ({"spi"}, {"sck": "spisd_sck", "miso": "spisd_miso", "mosi": "spisd_mosi", "cs": "spisd_cs"}),
                "sdio": ({"sd1", "sd4"}, {"clk": "sdio_ck", "cmd": "sdio_cmd", "dat0": "sdio_d0", "dat1": "sdio_d1",
                                          "dat2": "sdio_d2", "dat3": "sdio_d3"})}


def pads_of(request, target, feature):
    """{role: {"pad", "pin"}}: the probe's labelled lines, else the record's parts.sd; skip when neither has them."""
    found, missing = lines(host_of(request), target, feature)
    if not missing:
        return found
    buses, roles = RECORD_ROLES[feature]
    sd = (target.get("record") or {}).get("parts", {}).get("sd") or {}
    if feature == "spisd" and sd.get("bus") == "sd4":
        # An SDIO slot in SPI mode: CLK is SCK, CMD MOSI, DAT0 MISO and DAT3 the chip select (the sketch falls back
        # to SoftSPI where no SPI instance reaches the pads).
        sd = dict(bus="spi", sck=sd["clk"], mosi=sd["cmd"], miso=sd["dat0"], cs=sd["dat3"])
    if sd.get("bus") in buses:
        return {roles[k]: dict(pad=v.upper(), pin=pin_number(v)) for k, v in sd.items() if k in roles}
    pytest.skip(f"{feature}: the probe labels no " + ", ".join(missing)
                + f" and the record's parts.sd is not on bus {' / '.join(sorted(buses))}")

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
    """The OEP session, or None when the port is not an OEP probe's (--port is the plugin's monitor wrapping by now;
    --flash-port keeps the address as given)."""
    address = request.config.getoption("flash_port", None) or request.config.getoption("port", None) or ""
    if "oep://" not in str(address):
        return None
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


def test_sd_sdio(fresh_card, dut, request, record_property):
    case = settings(__file__)
    pads = pads_of(request, case["target"], "sdio")
    ready(dut, case)
    p = {role: line["pin"] for role, line in pads.items()}
    wide = all(f"sdio_d{i}" in p for i in (1, 2, 3))
    pins = [p["sdio_ck"], p["sdio_cmd"], p["sdio_d0"]] + ([p["sdio_d1"], p["sdio_d2"], p["sdio_d3"]] if wide else [])
    m = ask(dut, "sdio " + " ".join(map(str, pins)) + " 20000", rb"SDIO (\d) (\d) (\d+) (\d) (\d+) (\d)", timeout=20)
    assert m.group(1) == b"1", ("no card answered, the pads are not the series' SDIO pins, or no FAT volume (a card "
                                "that answered SPI keeps to SPI until its power goes)")
    record_property("sdio_card", CARD_TYPES.get(int(m.group(2)), m.group(2).decode()))
    record_property("sdio_clock_khz", int(m.group(5)))
    assert int(m.group(6)) == (4 if wide else 1)
    files(dut, record_property, "sdio")


@pytest.fixture
def fresh_card():
    """Before the test's dut (set up after this): when the card is an SDIO slot's on a board behind a WCH-Link (the
    record's parts.sd, bus sd1 / sd4), power the Link's USB port off and on. A card that answered in one bus mode
    keeps to it until its power goes: after SPI it ignores SDIO, and after SDIO it ignores CMD0 with CS low
    (nanoCH32H417, 2026-11-14, both seen). Its supply is the board's, so the port is the only switch. On an OEP probe
    the port is left alone: the probe would go with it."""
    target = settings(__file__)["target"]
    sd = (target.get("record") or {}).get("parts", {}).get("sd") or {}
    if target.get("kind") != "oep" and sd.get("bus") in ("sd1", "sd4"):
        hub, ports = target["power_hub"], ",".join(map(str, target["power_ports"]))
        subprocess.run(["uhubctl", "-l", hub, "-p", ports, "-a", "off"], capture_output=True, check=True)
        time.sleep(2)
        subprocess.run(["uhubctl", "-l", hub, "-p", ports, "-a", "on"], capture_output=True, check=True)
        port = pathlib.Path(target["port"])
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline and not port.exists():
            time.sleep(0.2)
        time.sleep(1)
    yield


def test_sd_spi(fresh_card, dut, request, record_property):
    case = settings(__file__)
    pads = pads_of(request, case["target"], "spisd")
    ready(dut, case)
    p = {role: line["pin"] for role, line in pads.items()}
    m = ask(dut, f"spi {p['spisd_sck']} {p['spisd_miso']} {p['spisd_mosi']} {p['spisd_cs']} 4000000",
            rb"SPI (\d) (\d) (\d+) (\d) (\w+)", timeout=20)
    assert m.group(1) == b"1", "no card answered, or it holds no FAT volume"
    record_property("spisd_card", CARD_TYPES.get(int(m.group(2)), m.group(2).decode()))
    record_property("spisd_bus", m.group(5).decode())
    assert int(m.group(3)) > 0 and int(m.group(4)) > 0
    files(dut, record_property, "spisd")
