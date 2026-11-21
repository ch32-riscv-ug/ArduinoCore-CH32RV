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
import zlib

import pytest

import sys

sys.path.insert(0, str(pathlib.Path(__file__).parent))   # bench_lines.py is staged beside the test
from bench_lines import lines, pin_number
import bench_ready   # staged beside the test (run_hardware.py)

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
    dut.write(line)
    return dut.expect(reply + rb"\r?\n", timeout=timeout)


def ready(dut, case):
    nonce = secrets.token_hex(4)
    m = bench_ready.ready(dut, rb" (\S+)", nonce=nonce)
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


SDIO_REPLY = rb"SDIO (\d) (\d) (\d+) (\d) (\d+) (\d) (\d) (\d)"


def sdio_pins(request, case, one_bit=False):
    """The SDIO pads as the sketch's pin numbers, D1-D3 when labelled (and not one_bit), and the bus width."""
    pads = pads_of(request, case["target"], "sdio")
    p = {role: line["pin"] for role, line in pads.items()}
    wide = not one_bit and all(f"sdio_d{i}" in p for i in (1, 2, 3))
    pins = [p["sdio_ck"], p["sdio_cmd"], p["sdio_d0"]] + ([p["sdio_d1"], p["sdio_d2"], p["sdio_d3"]] if wide else [])
    return pins, (4 if wide else 1)


def mount_sdio(dut, pins, khz, dma=1, hwfc=1):
    """sdopt, then sdio: (card type, clock kHz, bits, high speed, dma) of a card that mounted."""
    ask(dut, f"sdopt {dma} {hwfc}", rb"SDOPT")
    m = ask(dut, "sdio " + " ".join(map(str, pins)) + f" {khz}", SDIO_REPLY, timeout=20)
    assert m.group(1) == b"1", ("no card answered, the pads are not the series' SDIO pins, or no FAT volume (a card "
                                "that answered SPI keeps to SPI until its power goes)")
    return (int(m.group(2)), int(m.group(5)), int(m.group(6)), m.group(7) == b"1", m.group(8) == b"1")


def test_sd_sdio(fresh_card, dut, request, record_property):
    case = settings(__file__)
    pins, bits = sdio_pins(request, case)
    ready(dut, case)
    card, khz, got_bits, _, _ = mount_sdio(dut, pins, 20000)
    record_property("sdio_card", CARD_TYPES.get(card, str(card)))
    record_property("sdio_clock_khz", khz)
    assert got_bits == bits
    # 20000 kHz is default speed's ceiling or below it: no switch asked. The clock is HCLK / n under it.
    assert khz <= 20000
    files(dut, record_property, "sdio")


BIG_KB = 1024


def expected_crc(kb):
    """CRC-32 (zlib's) of the sketch's big file: block i of 2048 bytes, byte k = (i * 7 + k) & 0xff."""
    crc = 0
    for i in range(kb * 1024 // 2048):
        crc = zlib.crc32(bytes(((i * 7 + k) & 0xFF) for k in range(2048)), crc)
    return crc


def big(dut, record_property, label):
    m = ask(dut, f"big {BIG_KB}", rb"BIG (\d) (\d+) (\d+) ([0-9a-f]{8}) ([0-9a-f]{8}) (\d+) (\d+)", timeout=120)
    assert m.group(1) == b"1", "write, read back or remove failed"
    want = expected_crc(BIG_KB)
    assert int(m.group(4), 16) == want, "the DUT's pattern is not the host's"
    assert int(m.group(5), 16) == want, "the bytes read back differ from those written"
    wus, rus = int(m.group(2)), int(m.group(3))
    # KB/s of the write() / read() calls alone (FatFS's FAT and directory updates included).
    record_property(f"{label}_write_kbps", round(BIG_KB * 1e6 / max(wus, 1)))
    record_property(f"{label}_read_kbps", round(BIG_KB * 1e6 / max(rus, 1)))
    record_property(f"{label}_dma_blocks", int(m.group(6)))
    record_property(f"{label}_cpu_blocks", int(m.group(7)))
    return int(m.group(6)), int(m.group(7))


# (label, dma, hardware flow control, 1-bit): DMA and the CPU on the same card and clock, 4 and 1 lines, and the
# DMA without flow control - the last only recorded: with the clock running on, the DMA's latency against the
# 32-word FIFO decides it (an overrun fails the transfer), which is what it measures.
SDIO_PATHS = [("dma", 1, 1, False), ("cpu", 0, 1, False), ("dma1bit", 1, 1, True), ("dmanofc", 1, 0, False)]


@pytest.mark.parametrize("label,dma,hwfc,one_bit", SDIO_PATHS, ids=[p[0] for p in SDIO_PATHS])
def test_sd_sdio_transfer(fresh_card, dut, request, record_property, label, dma, hwfc, one_bit):
    """A 1 MB file written and read back through FatFS, its CRC-32 checked against the host's; which path moved the
    blocks (the DUT counts them) and how fast, recorded. 1-bit runs only where the slot has D1-D3 too."""
    case = settings(__file__)
    pins, bits = sdio_pins(request, case, one_bit=one_bit)
    if one_bit and len(sdio_pins(request, case)[0]) == 3:
        pytest.skip("the slot has D0 only: the 1-bit run is test_sd_sdio_transfer[dma]")
    ready(dut, case)
    _, khz, got_bits, hs, dma_on = mount_sdio(dut, pins, 20000, dma=dma, hwfc=hwfc)
    assert got_bits == bits
    record_property(f"sdio_{label}_clock_khz", khz)
    assert dma_on == bool(dma), "the DMA channel was held by someone else" if dma else "DMA on though asked off"
    if hwfc:
        dma_blocks, cpu_blocks = big(dut, record_property, f"sdio_{label}")
    else:
        try:
            dma_blocks, cpu_blocks = big(dut, record_property, f"sdio_{label}")
        except AssertionError as e:
            record_property(f"sdio_{label}_failed", str(e).splitlines()[0])
            return
    # The big file's blocks are whole sectors from a word-aligned buffer: by DMA when it is on. FatFS's own
    # sector buffer (FAT, directory) may sit anywhere, so some CPU blocks are allowed - fewer than the file's.
    file_blocks = 2 * BIG_KB * 1024 // 512
    if dma:
        assert dma_blocks >= file_blocks, f"only {dma_blocks} of the {file_blocks} file blocks went by DMA"
    else:
        assert dma_blocks == 0 and cpu_blocks >= file_blocks
    ask(dut, "end", rb"END")


def test_sd_sdio_high_speed(fresh_card, dut, request, record_property):
    """A ceiling of 50 MHz: the DUT asks the card for high speed (CMD6) and reports whether it switched; the clock is
    then HCLK / n at or below 50 MHz, else at or below default speed's 25 (the host does not know HCLK: the clock is
    recorded). Every SDHC / SDXC card (SD 2.0 and later) has high speed; an SDSC 1.x card may not, and is recorded.
    The 1 MB file then runs at that clock."""
    case = settings(__file__)
    pins, bits = sdio_pins(request, case)
    ready(dut, case)
    card, khz, got_bits, hs, _ = mount_sdio(dut, pins, 50000)
    record_property("sdio_hs", int(hs))
    record_property("sdio_hs_clock_khz", khz)
    assert got_bits == bits
    if card == 3:
        assert hs, "an SDHC / SDXC card did not take high speed"
    if hs:
        assert khz <= 50000
    else:
        assert khz <= 25000
    big(dut, record_property, "sdio_hs")
    ask(dut, "end", rb"END")


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
