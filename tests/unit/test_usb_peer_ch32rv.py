"""Another CH32RV board as the USB peer (the record's usb {to: ch32rv, port, controller}): the record's checks, the
cases a place gets with one, the peer resolved from its own address (harness/place.py), what run_hardware.py stages
for it, and that its sketches speak the command protocol the Pico's do (tests/peer/usb). No probe is opened."""
import json
import pathlib
import re
import sys

import pytest

REPO = pathlib.Path(__file__).resolve().parents[2]
TESTS = REPO / "tests"
sys.path.insert(0, str(TESTS))
sys.path.insert(0, str(TESTS / "peer/usb/device"))
sys.path.insert(0, str(TESTS / "peer/usb/host"))
import run_hardware  # noqa: E402
from harness import place, records  # noqa: E402

DUT = "oep://30eda0e31108/h417-a"
PEER = "oep://30eda0e31108/h417-c"
H417_A = dict(sku="CH32H417QEU6", family="CH32H417", uid="aaaaaaaaaaaaaaaa", chip_id="0x41700000")
H417_C = dict(sku="CH32H417QEU6", family="CH32H417", uid="cccccccccccccccc", chip_id="0x41700000")
X035_F8 = dict(sku="CH32X035F8U6", family="CH32X035", uid="1111111111111111", chip_id="0x03500000")


def ch32rv(controller="super_speed", port=PEER):
    return {"to": "ch32rv", "port": port, "controller": controller}


def test_the_record_names_the_peer_by_its_address_and_controller():
    assert records.check_core({"usb": ch32rv()}) == []
    assert records.check_core({"usb": ch32rv("full_speed", "wchlink://FB868F068059")}) == []
    assert records.check_core({"usb": ch32rv(port="oep://30eda0e31108/h417-c/ch32h417-cccccccccccccccc")}) == []
    # Per controller, beside a port to the host.
    assert records.check_core({"usb": {"super_speed": ch32rv(), "full_speed": {"to": "host"}}}) == []
    for bad, what in ((ch32rv(port="h417-c"), "usb.port"),                  # an address, not a port of this probe
                      (ch32rv(port="/dev/ttyACM0"), "usb.port"),
                      ({"to": "ch32rv", "port": PEER}, "usb.controller"),
                      (ch32rv("fs"), "usb.controller")):
        problems = records.check_core({"usb": bad})
        assert problems and problems[0].startswith(what), problems
    assert "ch32rv" in records.check_core({"usb": {"to": "nope"}})[0]
    assert records.check_core({"usb": {"super_speed": {"to": "ch32rv", "port": "x"}}})[0].startswith("usb.super_speed.port")


def test_the_cases_with_a_ch32rv_peer():
    items, caps = place.series_needs()
    h417 = dict(chip="CH32H417QEU6", kind="oep", lines={}, record={"usb": {"super_speed": ch32rv()}},
                usb_peer_chip="CH32H417QEU6")
    cases = place.choose_cases(h417, items, caps)
    for case in ("usb-peer", "usb-peer-classes", "usb-peer-fs", "usb-peer-input", "usb-peer-host",
                 "usb-peer-host-classes", "usb-peer-host-input"):
        assert cases[case] == "run", case
    # What the CH32RV libraries cannot serve yet, said so.
    for case in ("usb-peer-media", "usb-peer-net", "usb-peer-host-storage"):
        assert cases[case] == place.CH32RV_PEER_LACKS[case]
    # The peer must be a host for the device cases: not on its USBD, not on a part without one.
    cases = place.choose_cases(dict(h417, record={"usb": {"full_speed": ch32rv("usbd")}}), items, caps)
    assert cases["usb-peer"] == "skip: the CH32RV peer's usbd is no USB host" and cases["usb-peer-host"] == "run"
    cases = place.choose_cases(dict(h417, record={"usb": ch32rv("full_speed")}, usb_peer_chip="CH32X035F8U6"), items, caps)
    assert cases["usb-peer"] == "skip: the CH32RV peer CH32X035F8U6 has no USB host CH32RVUsbHost drives"
    assert cases["usb-peer-host-input"] == "run"
    # A DUT without a host still runs the device cases against a CH32RV host.
    f8 = dict(chip="CH32X035F8U6", kind="oep", lines={}, record={"usb": ch32rv("full_speed")}, usb_peer_chip="CH32H417QEU6")
    cases = place.choose_cases(f8, items, caps)
    assert cases["usb-peer"] == "run" and cases["usb-peer-host"].startswith("skip: CH32X035F8U6 has no USB host")


def fake_bench(monkeypatch, tmp_path, *, dut_record, peer_record=None, peer_port_record=None, peer_info=H417_C):
    """resolve() with the probe, sysfs and ch32rv stubbed per address."""
    monkeypatch.setattr(place, "usb_device", lambda serial, sysfs=None: ("3-1.4.2", "1209", "4f45", serial))
    facts = {DUT: (dut_record, {}), PEER: (peer_record or {}, peer_port_record or {})}

    def probe_facts(address, parsed):
        record, port_record = facts[address]
        return dict(port=parsed["port"], labels=[], lines={}, pads={}, port_record=port_record, record=record,
                    warnings=[], snapshot={})
    monkeypatch.setattr(place, "probe_facts", probe_facts)
    monkeypatch.setattr(place, "chip_info", lambda tool, address, part, log, env: dict(
        H417_A if address == DUT else peer_info))
    monkeypatch.setattr(place, "probe_capabilities", lambda tool, a, part, log, env: {})
    return place.resolve(DUT, "ch32rv", None, tmp_path / "logs")


def test_the_peer_is_resolved_from_its_own_address(monkeypatch, tmp_path):
    p = fake_bench(monkeypatch, tmp_path, dut_record={"usb": {"super_speed": ch32rv()}},
                   peer_record={"board": "wch:nanoCH32H417", "options": {"vio18": "3v3"}},
                   peer_port_record={"target": "ch32h417-cccccccccccccccc"})
    assert p["usb_peer"] == "ch32rv" and p["usb_peer_address"] == PEER
    assert p["usb_peer_controller"] == "super_speed" and p["usb_peer_ch32rv_controller"] == "super_speed"
    assert p["usb_peer_chip"] == "CH32H417QEU6" and p["usb_peer_board"] == "wch:nanoCH32H417"
    assert p["usb_peer_fqbn"].startswith("ch32-riscv-ug:ch32rv:CH32H417:pnum=CH32H417QEU6") and "vio18=3v3" in p["usb_peer_fqbn"]
    assert p["usb_link_speed"] == 3
    assert p["cases"]["usb-peer"] == "run" and p["cases"]["usb-peer-host"] == "run"
    # The slower side's speed: the DUT's FS against the peer's SS port.
    p = fake_bench(monkeypatch, tmp_path / "b", dut_record={"usb": {"full_speed": ch32rv()}})
    assert p["usb_link_speed"] == 1
    # The single form leaves the DUT's controller to the series: no speed is claimed.
    assert "usb_link_speed" not in fake_bench(monkeypatch, tmp_path / "c", dut_record={"usb": ch32rv()})


def test_a_wrong_peer_is_refused(monkeypatch, tmp_path):
    with pytest.raises(place.PlaceError, match="another board"):
        fake_bench(monkeypatch, tmp_path, dut_record={"usb": ch32rv()},
                   peer_port_record={"target": "ch32h417-0123456789abcdef"})
    with pytest.raises(place.PlaceError, match="the DUT itself"):
        fake_bench(monkeypatch, tmp_path / "b", dut_record={"usb": ch32rv()}, peer_info=H417_A)
    with pytest.raises(place.PlaceError, match="its core record has errors"):
        fake_bench(monkeypatch, tmp_path / "c", dut_record={"usb": ch32rv()}, peer_record={"supply": {"from": "x"}})


def test_a_peer_on_a_wch_link_takes_its_record_from_the_environment(monkeypatch, tmp_path):
    monkeypatch.setattr(place, "chip_info", lambda tool, address, part, log, env: dict(X035_F8))
    env = {"OEP_PORT_MEMO_FB868F068059": json.dumps({"board": "weact:CH32X035-CoreBoard-V1.0"})}
    peer = place.peer_place("wchlink://fb868f068059", "ch32rv", None, tmp_path, environ=env)
    assert peer["chip"] == "CH32X035F8U6" and peer["board"] == "weact:CH32X035-CoreBoard-V1.0"
    assert peer["fqbn"].startswith("ch32-riscv-ug:ch32rv:CH32X035:pnum=CH32X035F8U6")


TARGET = dict(fqbn="ch32-riscv-ug:ch32rv:CH32H417:pnum=CH32H417QEU6", chip="CH32H417QEU6", f_cpu=400_000_000,
              upload_port=DUT, usb_peer="ch32rv", usb_peer_address=PEER, usb_peer_controller="super_speed",
              usb_peer_ch32rv_controller="super_speed", usb_peer_chip="CH32H417QEU6",
              usb_peer_fqbn="ch32-riscv-ug:ch32rv:CH32H417:pnum=CH32H417QEU6,debugspeed=low", usb_link_speed=3)


def test_the_runner_stages_the_peer_sketch_for_its_board(tmp_path):
    module, _id, staged = run_hardware.stage_case(tmp_path, "t", "usb-peer", dict(TARGET))
    case = pathlib.Path(module).parent
    peer = case / "peer_ch32rv"
    assert not (case / "peer_s3").exists() and not (case / "peer_pico").exists()
    assert (peer / "peer_ch32rv.ino").exists() and (peer / "peer_fat.h").exists()
    yaml = (peer / "sketch.yaml").read_text()
    assert f'fqbn: "{TARGET["usb_peer_fqbn"]}"' in yaml and "chip: CH32H417QEU6" in yaml
    assert "source: dmseq" in yaml and "speed: low" in yaml and "default_profile: ch32rv" in yaml
    assert (peer / "build_opt.h").read_text() == "-DPEER_USB_HOST_PORT=CH32RV_USB_HOST_PORT_SUPER_SPEED\n"
    assert staged["usb_peer_port"] == PEER
    # The DUT's own controller, as before.
    assert "CH32RVUsbController::SuperSpeed" in (case / "build_opt.h").read_text()
    assert json.loads((case / "case.json").read_text())["target"]["usb_link_speed"] == 3

    module, _id, _ = run_hardware.stage_case(tmp_path, "t", "usb-peer-host", dict(TARGET, usb_peer_ch32rv_controller="usbd"))
    peer = pathlib.Path(module).parent / "peer_ch32rv"
    assert (peer / "build_opt.h").read_text() == "-DPEER_USB_CONTROLLER=CH32RVUsbController::Usbd\n"
    assert not (peer / "peer_fat.h").exists()
    with pytest.raises(RuntimeError, match="no USB host"):
        run_hardware.stage_case(tmp_path, "t", "usb-peer-fs", dict(TARGET, usb_peer_ch32rv_controller="usbd"))


def commands(sketch):
    """The console commands a peer sketch takes (its strcmp(cmd, "...") branches)."""
    return set(re.findall(r'strcmp\(cmd, "([^"]+)"\)', sketch.read_text()))


@pytest.mark.parametrize("family", ["device", "host_hid", "host_classes", "host_input", "host_storage"])
def test_the_peer_sketches_take_the_picos_commands(family):
    pico = TESTS / f"sketch_support/usb_peer/{family}/peer_pico/peer_pico.ino"
    mine = TESTS / f"sketch_support/usb_peer/{family}/peer_ch32rv/peer_ch32rv.ino"
    assert commands(pico) <= commands(mine), commands(pico) - commands(mine)
    # Every answer carries the tag the tests expect of this peer (device_peer.Peer / host_peer.IDENTITY).
    import device_peer
    import host_peer
    tag = (device_peer.Peer.TAGS if family == "device" else {k: v["tag"] for k, v in host_peer.IDENTITY.items()})["ch32rv"]
    lines = [a for a in re.findall(r'(?:say|print|println)\("([^"]*)', mine.read_text()) if a[:1].isupper()]
    assert lines and all(a.startswith(tag.decode()) for a in lines), [a for a in lines if not a.startswith(tag.decode())]


def test_every_staged_case_has_its_peer_sketch():
    for case, directory in run_hardware.CH32RV_PEERS.items():
        assert (directory / "peer_ch32rv.ino").exists(), case
        assert case in place.PEER_DEVICE_CASES + place.PEER_HOST_CASES
    # Each case place.py runs with a CH32RV peer has a sketch to stage.
    runnable = [c for c in place.PEER_DEVICE_CASES + place.PEER_HOST_CASES if c not in place.CH32RV_PEER_LACKS]
    assert set(runnable) <= set(run_hardware.CH32RV_PEERS)
