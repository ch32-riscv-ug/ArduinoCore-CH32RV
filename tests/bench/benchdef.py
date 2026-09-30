"""The bench behind a profile: tests/benches/<name>.toml, chosen by TEST_BENCH_<PROFILE>.

A bench file is the jig's design - probe model and firmware, the DUT, which DUT UART
the probe hears, what pad is wired to which probe channel, the probe's slot / binds /
plan, and the facts measured on it. The machine adds only the individual and its port
(TEST_SERIAL_PORT_<PROFILE>) in .env. Tests ask by pad name and skip, with the reason,
when the pad is not wired; nothing about a jig is written into a test.

`check()` compares a live probe with its bench file. It is what tests/bench/conftest.py
runs before the first upload and what `prepare.py --check` prints; a mismatch there is a
bench that needs `prepare.py --profile <board>`, not a core regression.

Plain module, loaded with tests/loader.py's load() or imported from tests/bench.
"""
from __future__ import annotations

import dataclasses
import os
import pathlib
import re
import tomllib
from typing import Any

HERE = pathlib.Path(__file__).resolve().parent
BENCHES = HERE.parent / "benches"


class BenchError(Exception):
    """The bench is not set up: a missing file or variable, or a probe that does not match."""


@dataclasses.dataclass
class Bench:
    name: str
    profile: str
    port: str
    data: dict[str, Any]

    @property
    def probe(self) -> dict:
        return self.data["probe"]

    @property
    def uart(self) -> tuple[int, int]:
        u = self.data["uart"]
        return int(u["usart"]), int(u["route"])

    @property
    def wiring(self) -> dict[str, Any]:
        return self.data.get("wiring", {})

    @property
    def facts(self) -> dict[str, Any]:
        return self.data.get("facts", {})

    def channel(self, pad: str) -> int:
        """The probe channel wired to `pad`, or BenchError naming the bench (a test turns that into a skip)."""
        value = self.wiring.get(pad)
        if not isinstance(value, int):
            raise BenchError(f"{pad} is not wired to a probe channel on {self.name}")
        return value

    def pad(self, channel: int) -> str:
        return next((p for p, ch in self.wiring.items() if ch == channel), f"channel {channel}")

    @property
    def probe_serial(self) -> str:
        """The probe's identity from the port: `oep://<unit id>/<slot>` (the unit id is the USB serial since
        oep-probe-arduino 0.0.19) or `wchlink://<serial>`; a serial OEP probe (`[probe] transport = "serial"`, an
        ESP32's UART bridge) is its device path."""
        if self.probe.get("transport") == "serial":
            return self.port
        m = re.match(r"^(oep|wchlink)://([^/]+)", self.port)
        if not m:
            raise BenchError(f"{self.profile}: TEST_SERIAL_PORT is {self.port!r}; a bench needs oep:// or wchlink://")
        return m.group(2)


def env_key(profile: str, suffix: str = "") -> str:
    return f"TEST_{suffix}{profile.upper().replace('-', '_')}"


def load(profile: str, port: str | None = None) -> Bench:
    """The bench for `profile`, from TEST_BENCH_<PROFILE> and tests/benches/. Raises BenchError when unset."""
    name = os.environ.get(env_key(profile, "BENCH_"))
    if not name:
        raise BenchError(f"{env_key(profile, 'BENCH_')} is not set: which tests/benches/<name>.toml is behind "
                         f"--profile {profile} (see tests/.env.example)")
    path = BENCHES / f"{name}.toml"
    if not path.exists():
        raise BenchError(f"{path} does not exist ({env_key(profile, 'BENCH_')}={name})")
    data = tomllib.loads(path.read_text(encoding="utf-8"))
    board = data.get("dut", {}).get("board", "")
    if board.lower() != profile.lower():
        raise BenchError(f"{path.name} is a {board} bench, but the profile is {profile}")
    port = port or os.environ.get(env_key(profile, "SERIAL_PORT_")) or ""
    if not port:
        raise BenchError(f"{env_key(profile, 'SERIAL_PORT_')} is not set")
    return Bench(name, profile, port, data)


def find_ch32rv() -> pathlib.Path | None:
    """A ch32rv for the checks (`broker endpoint`, `probe list`): the newest the platform installed
    (~/.arduino15, profile installs included), else <repo>/.tools, else PATH. Both commands are in ch32rv's
    stable JSON contract, so the exact version does not matter here."""
    import shutil
    home = pathlib.Path.home() / ".arduino15"
    found = list(home.glob("packages/ch32-riscv-ug/tools/ch32rv/*/ch32rv")) + \
        list(home.glob("internal/ch32-riscv-ug_ch32rv_*/ch32rv")) + list((HERE.parents[1] / ".tools" / "ch32rv").glob("*/ch32rv"))

    def version(p: pathlib.Path) -> tuple:
        m = re.search(r"(\d+)\.(\d+)\.(\d+)", p.parent.name)
        return tuple(int(x) for x in m.groups()) if m else (0, 0, 0)
    if found:
        return max(found, key=version)
    which = shutil.which("ch32rv")
    return pathlib.Path(which) if which else None


# ---------------------------------------------------------------- the live probe

def _text(v: bytes) -> str:
    s = v.decode("ascii", "replace")
    return s if s.isprintable() and s else v.hex()


def oep_probe_facts(hst) -> dict[str, str]:
    """firmware / model / unit / profile from oep.core's describe (lock-free)."""
    from oep_client import core, registry
    tags = registry.CORE.tlv["describe"]
    want = {tags["firmware"]: "firmware", tags["model"]: "model", tags["unit_id"]: "unit", tags["profile"]: "profile"}
    out = {}
    for tag, value in core.describe(hst, 0):
        key = want.get(tag & 0x7F)
        if key:
            out[key] = _text(value)
    return out


def oep_config_items(hst) -> list:
    """The probe's settings, decoded (lock-free read)."""
    from oep_client import config
    return config.ProbeConfig(hst).items()


def wanted_items(bench: Bench, hst) -> list:
    """The bench file's slot / binds / plan as oep_client.config items, in the probe's canonical order
    (plans, then slots, then binds - what `oep config show` prints)."""
    from oep_client import config, core, registry
    items = []
    slot = bench.data.get("slot")
    for spec in bench.data.get("plan", []):
        name, _, k = spec["interface"].partition("#")
        fns = core.find_all(hst, name)
        fn = fns[int(k or 1) - 1]
        roles = registry.by_name(name).enum.get("role", {}) if hasattr(registry, "by_name") else {}
        for role, ch in spec["roles"].items():
            number = roles.get(role) if roles else {"rx": 1, "tx": 2, "line": 1}.get(role)
            items.append(config.Plan(fn=fn, role=int(number), channel=int(ch)))
    for spec in bench.data.get("idle", []):          # oep config idle: how an unassigned channel rests
        items.append(config.Idle(channel=int(spec["channel"]), mode=spec.get("mode", "pull-up")))
    if slot:
        wire_fn = core.find(hst, f"oep.wire.{slot['wire']}")
        pins = tuple(slot["pins"]) if len(slot["pins"]) == 2 else (slot["pins"][0], 0xFFFF)
        items.append(config.Slot(slot=0, wire_fn=wire_fn, pins=pins, name=slot["name"],
                                 attach=slot.get("attach", "host"), retry_s=int(slot.get("retry_s", 0)),
                                 mechanism=slot.get("mechanism", "dmseq"), lock=None,
                                 max_speed=int(slot.get("max_speed", 0)), idle_clock=slot.get("idle_clock", "high")))
    for b in bench.data.get("bind", []):
        streams = []
        for s in b["streams"]:
            kind, _, ref = s.partition(":")
            if kind == "slot":
                streams.append(("slot", 0))
            else:
                streams.append(("uart", core.find_all(hst, "oep.fixture.uart")[int(ref) - 1]))
        items.append(config.Bind(port=int(b["port"]), mode=b.get("mode", "last-reset"), streams=streams,
                                 selected=int(b.get("selected", 0))))
    return items


def attach_slot(bench: Bench, hst, halt: bool = False):
    """A test's own connection on the slot's wire, joining the slot's live connection. The host picks the wire's
    pins since oep-probe-arduino 0.0.8; an attach that names none joins the one live connection on that wire
    (oep-if-debug §1, from 0.0.9 - 0.0.8 wanted the pair named). -> (wire, connection)."""
    from oep_client import riscv
    slot = bench.data["slot"]
    wire = riscv.Wire(hst, "oep.wire." + slot["wire"])
    conn, _ = wire.attach(halt=halt)
    return wire, conn


def open_probe(bench: Bench, ch32rv: pathlib.Path | str | None = None):
    """An oep_client Host on the bench's OEP probe: through ch32rv's broker when one is running for this port
    (it holds the vendor bulk then), else straight over USB, or over the probe's serial port when the bench
    says `transport = "serial"`. -> (host, close)."""
    from oep_client import link
    endpoint = None
    if ch32rv:
        import json
        import subprocess
        out = subprocess.run([str(ch32rv), "broker", "endpoint", "--probe", f"port:{bench.port}", "--json"],
                             capture_output=True, text=True, timeout=30)
        try:
            endpoint = json.loads(out.stdout).get("result", {}).get("endpoint")
        except json.JSONDecodeError:
            endpoint = None
    if endpoint:
        hst = link.open_host(f"tcp://{endpoint}")
    elif bench.probe.get("transport") == "serial":
        hst = link.open_host(bench.port)
    else:
        # by the unit id (= the USB serial since oep-probe-arduino 0.0.19), whatever VID:PID the probe carries
        hst = link.open_host(f"usb:{bench.probe_serial}")
    return hst, hst.link.close


def check(bench: Bench, ch32rv: pathlib.Path | str | None = None) -> list[str]:
    """Compare the live probe with the bench file. -> the mismatches, in words; empty means ready."""
    kind = bench.probe.get("kind", "oep")
    if kind == "wchlink":
        return _check_wchlink(bench, ch32rv)
    if kind != "oep":
        return [f"{bench.name}: unknown probe kind {kind!r}"]
    problems = []
    hst, close = open_probe(bench, ch32rv)
    try:
        facts = oep_probe_facts(hst)
        for key in ("model", "firmware", "profile"):
            want = bench.probe.get(key)
            if want and facts.get(key) != want:
                problems.append(f"probe {key}: bench file says {want!r}, probe says {facts.get(key)!r}")
        # The settings the probe runs on have to be the SAVED ones: items() shows what is live, which right after
        # prepare is what was just set - but a reboot that cannot read the storage (a firmware whose interface
        # list no longer matches the saved hash, oep-probe-arduino <= 0.0.16) runs on nothing, and only the storage
        # state says so.
        from oep_client import config
        st = config.ProbeConfig(hst).state()
        storage = config.STORAGE_STATE.get(st.storage, st.storage)
        if storage != "applied":
            problems.append(f"probe settings storage is {storage!r} (saved hash {st.saved_hash:#x}): the probe is not "
                            f"running its saved settings")
        have = [dataclasses.astuple(i) for i in oep_config_items(hst)]
        want_items = [dataclasses.astuple(i) for i in wanted_items(bench, hst)]
        for it in want_items:
            if it not in have:
                problems.append(f"probe config lacks {it}")
        for it in have:
            if it not in want_items:
                problems.append(f"probe config has {it}, not in the bench file")
    finally:
        close()
    return problems


def _check_wchlink(bench: Bench, ch32rv) -> list[str]:
    import json
    import subprocess
    if not ch32rv:
        return ["a WCH-Link bench is checked through ch32rv, and none was given"]
    out = subprocess.run([str(ch32rv), "probe", "list", "--json"], capture_output=True, text=True, timeout=60)
    probes = json.loads(out.stdout).get("result", {}).get("probes", [])
    mine = next((p for p in probes if p.get("serial") == bench.probe_serial), None)
    if mine is None:
        return [f"no WCH-Link with serial {bench.probe_serial} is attached"]
    problems = []
    if bench.probe.get("model") and mine.get("model") != bench.probe["model"]:
        problems.append(f"probe model: bench file says {bench.probe['model']!r}, probe says {mine.get('model')!r}")
    fw = (mine.get("firmware") or {}).get("norm")
    if bench.probe.get("firmware") and fw != bench.probe["firmware"]:
        problems.append(f"probe firmware: bench file says {bench.probe['firmware']!r}, probe says {fw!r}")
    return problems
