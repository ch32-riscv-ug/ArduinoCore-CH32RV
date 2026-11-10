"""A bench target from its address alone (tests/harness/place.py, records.py): what is refused before any hardware
operation, and which cases a place gets. No probe is opened: sysfs is a temporary tree and the chip is a stub."""
import json
import pathlib
import sys

import pytest

REPO = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tests"))
from harness import place, records  # noqa: E402


def sysfs(tmp_path, devices):
    root = tmp_path / "usb"
    for name, (vid, pid, serial) in devices.items():
        d = root / name
        d.mkdir(parents=True, exist_ok=True)
        (d / "idVendor").write_text(vid + "\n")
        (d / "idProduct").write_text(pid + "\n")
        (d / "serial").write_text(serial + "\n")
    return root


def test_addresses():
    assert place.parse("oep://30EDA0E31108/x035") == dict(kind="oep", unit="30eda0e31108", port="x035", target=None)
    assert place.parse("oep://30eda0e31108/x035/ch32x035-1FF9")["target"] == "ch32x035-1ff9"
    assert place.parse("wchlink://fb868f068059") == dict(kind="wchlink", serial="FB868F068059")
    for bad in ("oep://30eda0e31108", "/dev/ttyACM0", "oep://x/x035", "usb", "wchlink://"):
        with pytest.raises(place.PlaceError):
            place.parse(bad)


def test_the_probe_is_found_by_its_serial_only(tmp_path):
    root = sysfs(tmp_path, {"3-1.4.2": ("1209", "4f45", "30eda0e31108"), "3-3.3.4": ("1a86", "8010", "FB868F068059")})
    assert place.usb_device("30EDA0E31108", root) == ("3-1.4.2", "1209", "4f45", "30eda0e31108")
    with pytest.raises(place.PlaceError, match="not connected"):
        place.usb_device("30eda0e31109", root)
    root2 = sysfs(tmp_path / "b", {"1-1": ("1209", "4f45", "abc"), "1-2": ("1209", "4f45", "ABC")})
    with pytest.raises(place.PlaceError, match="several"):
        place.usb_device("abc", root2)


def test_fqbn_and_clock_come_from_boards_txt():
    assert place.fqbn_of("CH32X035C8T6") == (
        "ch32-riscv-ug:ch32rv:CH32X035:pnum=CH32X035C8T6,rstpin=reset,iwdg=sw", 48000000)
    assert place.fqbn_of("CH32V307VCT6")[0] == "ch32-riscv-ug:ch32rv:CH32V307:pnum=CH32V307VCT6,iwdg=sw"
    with pytest.raises(place.PlaceError):
        place.fqbn_of("CH32X999Z9")


def fake_resolve(monkeypatch, tmp_path, *, info, record=None, port_record=None, address="oep://30eda0e31108/x035"):
    root = sysfs(tmp_path, {"3-1.4.2": ("1209", "4f45", "30eda0e31108")})
    monkeypatch.setattr(place, "SYSFS", root)
    monkeypatch.setattr(place, "usb_device", lambda serial, sysfs=root: ("3-1.4.2", "1209", "4f45", serial))
    monkeypatch.setattr(place, "probe_facts", lambda a, p: dict(port="x035", labels=[], lines={"nrst": 32}, pads={},
                        port_record=port_record or {}, record=record or {}, warnings=[], snapshot={}))
    calls = []

    def chip(tool, address, part, log, env):
        calls.append(part)
        got = dict(info)
        if part and not got.get("sku"):
            got["sku"] = part
        return got
    monkeypatch.setattr(place, "chip_info", chip)
    return place.resolve(address, "ch32rv", None, tmp_path / "logs"), calls


X035 = dict(sku="CH32X035C8T6", family="CH32X035", uid="1ff9abcd880ebc48", chip_id="0x03510601")


def test_a_place_from_the_chip_and_the_records(monkeypatch, tmp_path):
    p, _ = fake_resolve(monkeypatch, tmp_path, info=X035, port_record={"target": "ch32x035-1ff9abcd880ebc48"},
                        record={"usb": {"to": "host"}, "defects": [{"id": "d", "lose": ["i2c"]}]})
    assert p["fqbn"] == "ch32-riscv-ug:ch32rv:CH32X035:pnum=CH32X035C8T6,rstpin=reset,iwdg=sw" and p["f_cpu"] == 48000000
    assert p["power_hub"] == "3-1.4" and p["power_ports"] == [2]   # only the probe's own port
    assert p["dut_usb_serial"] == "1FF9ABCD880EBC48" and p["nrst_channel"] == 32
    assert p["cases"]["usb"] == "run" and p["cases"]["usb-peer"].startswith("skip")
    assert p["cases"]["wire"] == "skip: defect d (i2c)"
    assert p["cases"]["sd"].startswith("skip") and p["cases"]["power"].startswith("skip")


def test_another_board_on_the_port_is_refused(monkeypatch, tmp_path):
    with pytest.raises(place.PlaceError, match="another board"):
        fake_resolve(monkeypatch, tmp_path, info=X035, port_record={"target": "ch32x035-0123456789abcdef"})
    with pytest.raises(place.PlaceError, match="the address names"):
        fake_resolve(monkeypatch, tmp_path, info=X035, address="oep://30eda0e31108/x035/ch32x035-0123456789abcdef")


def test_an_ambiguous_chip_takes_the_recorded_part_only_if_it_is_a_candidate(monkeypatch, tmp_path):
    v103 = dict(family="CH32V103", uid="1035abcd7869bc67", candidates=["CH32V103C8T6", "CH32V103R8T6"])
    with pytest.raises(place.PlaceError, match="leaves CH32V103C8T6, CH32V103R8T6"):
        fake_resolve(monkeypatch, tmp_path, info=v103)
    p, calls = fake_resolve(monkeypatch, tmp_path / "b", info=v103, port_record={"part": "CH32V103C8T6"})
    assert p["chip"] == "CH32V103C8T6" and calls == [None, "CH32V103C8T6"]
    with pytest.raises(place.PlaceError, match="not one of"):
        fake_resolve(monkeypatch, tmp_path / "c", info=v103, port_record={"part": "CH32V103R6T6"})


def test_a_bad_record_stops_before_the_chip_is_touched(monkeypatch, tmp_path):
    with pytest.raises(place.PlaceError, match="serial .* or, for a bridge without one, mac"):
        fake_resolve(monkeypatch, tmp_path, info=X035, record={"usb": {"to": "esp32-s3"}})


def test_peer_cases_and_usb_host_parts():
    items, caps = place.series_needs()
    f8 = dict(chip="CH32X035F8U6", kind="oep", lines={}, record={"usb": {"to": "esp32-s3", "serial": "58FA041040"}})
    cases = place.choose_cases(f8, items, caps)
    assert cases["usb-peer-fs"] == "run" and cases["usb-peer-host"] == "skip: CH32X035F8U6 has no USB host CH32RVUsbHost drives"
    c8 = dict(f8, chip="CH32X035C8T6")
    assert place.choose_cases(c8, items, caps)["usb-peer-host"] == "run"
    v307 = dict(chip="CH32V307VCT6", kind="wchlink", lines={}, record={})
    cases = place.choose_cases(v307, items, caps)
    assert cases["usb"] == "skip: the record's usb is not {to: host}" and cases["power"] == "skip: not on an OEP port"
    s3 = {"usb": {"to": "esp32-s3", "serial": "x"}}
    assert place.choose_cases(dict(v307, record=s3), items, caps)["usb-peer-host"] == "run"
    for chip in ("CH32V103R8T6",):   # the older USB: device, no host
        cases = place.choose_cases(dict(v307, chip=chip, record=s3), items, caps)
        assert cases["usb-peer"] == "run" and cases["usb-peer-host"].startswith("skip:"), chip
    for chip in ("CH32V407WEU6", "CH32X315MCU6"):   # the second-generation USBHS: a host too
        cases = place.choose_cases(dict(v307, chip=chip, record=s3), items, caps)
        assert cases["usb-peer"] == cases["usb-peer-host"] == "run", chip
    assert place.choose_cases(dict(v307, chip="CH32V003F4U6", record=s3), items, caps)["usb-peer"] == \
        "skip: CH32V003F4U6 has no USB controller"


def test_an_rp2040_peer_on_the_same_probe(monkeypatch, tmp_path):
    p, _ = fake_resolve(monkeypatch, tmp_path, info=X035, record={"usb": {"to": "rp2040", "port": "pico"}})
    assert p["usb_peer"] == "pico" and p["usb_peer_address"] == "oep://30eda0e31108/pico"
    assert p["cases"]["usb-peer"] == "run" and p["cases"]["usb-peer-host"] == "run"
    # The RP2040's host has no isochronous transfers.
    assert p["cases"]["usb-peer-media"].startswith("skip: the RP2040's USB host has no isochronous")
    with pytest.raises(place.PlaceError, match="usb.port"):
        fake_resolve(monkeypatch, tmp_path / "b", info=X035, record={"usb": {"to": "rp2040"}})
    s3, _ = fake_resolve(monkeypatch, tmp_path / "c", info=X035, record={"usb": {"to": "esp32-s3", "serial": "x"}})
    assert s3["usb_peer"] == "s3" and s3["cases"]["usb-peer-media"] == "run"


def test_an_esp32_p4_peer_on_the_usbhs():
    items, caps = place.series_needs()
    p4 = {"usb": {"to": "esp32-p4", "serial": "30:ED:A0:E3:14:F5"}}
    errors = []
    records._usb(p4["usb"], errors)
    assert errors == []
    v407 = dict(chip="CH32V407WEU6", kind="wchlink", lines={}, record=p4)
    cases = place.choose_cases(v407, items, caps)
    # Every device case runs (the P4's host has isochronous transfers), and the host cases the P4 has sketches for.
    assert all(cases[c] == "run" for c in place.PEER_DEVICE_CASES)
    assert cases["usb-peer-host"] == cases["usb-peer-host-classes"] == "run"
    assert cases["usb-peer-host-input"] == place.P4_LACKS["usb-peer-host-input"]


def test_an_esp32_peer_found_by_mac(tmp_path):
    # A CH340 bridge has no USB serial: the record carries the MAC, and board-identify's name finds the tty.
    errors = []
    records._usb({"to": "esp32-s3", "mac": "7cdfa1fc77d0"}, errors)
    assert errors == []
    records._usb({"to": "esp32-s3"}, errors)
    assert errors and "mac" in errors[0]
    (tmp_path / "esp32-s3-7cdfa1fc77d0").touch()
    (tmp_path / "esp32-s3-d0cf1358fd94").touch()
    assert place.peer_tty(None, mac="7cdfa1fc77d0", identify=tmp_path) == str(tmp_path / "esp32-s3-7cdfa1fc77d0")
    with pytest.raises(place.PlaceError, match="0 board-identify names"):
        place.peer_tty(None, mac="000000000000", identify=tmp_path)


def test_record_checks():
    assert records.check_core({"board": "wch:CH32X035C8T-R0", "supply": {"from": "probe"},
                               "parts": {"sd": {"bus": "sd1", "clk": "pa5", "cmd": "pa7", "dat0": "pa6"},
                                         "eth": {"to": "lan"}},
                               "someone.else": {"kept": True}}) == []
    errors = records.check_core({"board": "CH32X035", "parts": {"sd": {"bus": "spi", "sck": "PA5"}},
                                 "pd": {"vbus_pin": "pa1"}})
    assert any(e.startswith("board") for e in errors)
    assert any("lacks miso, mosi, cs" in e for e in errors) and any("PA5" in e for e in errors)
    assert any("go together" in e for e in errors)
    assert records.check_port({"target": "ch32x035-1ff9abcd880ebc48", "part": "CH32X035C8T6"}) == []
    assert records.check_port({"target": "CH32X035 1FF9"}) != []
    assert records.port_record_name("X035") == "oep.probe.memo/port/x035"
    assert records.core_record_name("X035") == "io.github.ch32-riscv-ug.arduino/port/x035"
    assert len(records.core_record_name("a" * 16)) <= 64


class FakeMemo:
    max_object = 2048

    def __init__(self, store):
        self.store = store

    def read(self, name):
        if name not in self.store:
            from oep_client import host as h, message as m
            raise h.Rejected(m.Result(0, m.REJECTED, m.NO_RESOURCE))
        return self.store[name]

    def write(self, name, data):
        self.store[name] = data

    def delete(self, name):
        del self.store[name]


def test_update_keeps_other_members_and_writes_only_changes(monkeypatch):
    pytest.importorskip("oep_client.memo")
    from oep_client import memo
    store = {"io.github.ch32-riscv-ug.arduino/port/x035": json.dumps({"board": "wch:A-1", "other.tool": 1}).encode()}
    monkeypatch.setattr(memo, "Memo", lambda host: FakeMemo(store))
    written = records.update(None, "X035", core_changes={"supply": {"from": "probe"}})
    after = json.loads(store["io.github.ch32-riscv-ug.arduino/port/x035"])
    assert after == {"board": "wch:A-1", "other.tool": 1, "supply": {"from": "probe"}} and len(written) == 1
    assert records.update(None, "x035", core_changes={"supply": {"from": "probe"}}) == {}
    with pytest.raises(ValueError, match="usb.to"):
        records.update(None, "x035", core_changes={"usb": {"to": "pc"}})
    records.update(None, "x035", port_changes={"target": "ch32x035-1ff9"})
    assert json.loads(store["oep.probe.memo/port/x035"]) == {"target": "ch32x035-1ff9"}


def test_a_part_without_a_peripheral_and_a_port_without_its_lines():
    from harness import series
    assert series.part_absent()["CH32M030"]["CH32M030G8R7"] == {"SPI1"}
    assert "USART1" not in series.part_capabilities("CH32X035", "CH32X035F8U6")
    items, caps = place.series_needs()
    g8 = dict(chip="CH32M030G8R7", kind="oep", port_name="m030", lines={"rx": 30, "swio": 31}, record={})
    cases = place.choose_cases(g8, items, caps)
    assert cases["spi"] == "skip: CH32M030G8R7 has no spi" and cases["softspi"] == "run"
    assert cases["uart"] == "skip: the probe labels no m030.tx"
    c8 = dict(g8, chip="CH32M030C8T7", lines={"rx": 30, "tx": 29})
    cases = place.choose_cases(c8, items, caps)
    assert cases["spi"] == "run" and cases["uart"] == "run"
    link = dict(g8, kind="wchlink", lines={})
    assert place.choose_cases(link, items, caps)["uart"] == "run"   # a WCH-Link's own UART bridge


def test_serial_routes_come_from_the_variant():
    from harness import series
    routes = series.serial_routes("CH32X035")
    assert (0, "PA2", "PA3") in routes[2] and (0, "PB10", "PB11") in routes[1]
    assert series.serial_routes("CH32NOPE") == {}
