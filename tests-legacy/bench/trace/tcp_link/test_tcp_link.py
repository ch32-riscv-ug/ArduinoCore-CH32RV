"""OEP over TCP: a probe that joins a network (the bench file's wifi = "env", the networks from OEP_WIFI_SSID_<n> /
OEP_WIFI_PASS_<n> in tests-legacy/.env, written by prepare --config) is the same probe on its TCP link as on its usual one.
The address comes from the probe's own state over the usual link (mDNS does not reach WSL). Over TCP:

  - describe gives the same unit_id;
  - a session attaches the slot's wire and reads the target (the riscv-dm, halted and resumed);
  - a fixture GPIO reads a wired pad;
  - the lock is one across both links: the usual link is refused while the TCP session holds it;
  - ch32rv uploads this sketch with --probe tcp:<ip>:<port>, and its monitor hears the new sketch's banner.

The sketch first goes up the usual way (the dut fixture), then again over TCP; the banner the TCP monitor hears is
the one the TCP upload's reset printed (the monitor opens after it).

  uv run --env-file .env --with pytest-embedded-arduino-cli-ch32rv pytest bench/trace/tcp_link --profile ch32v003
"""
import pathlib
import subprocess
import time

import pytest
from loader import load

kit = load("tests-legacy/bench/bench_kit.py", "bench_kit")
import benchdef  # noqa: E402  (bench/ is on sys.path: conftest)

BUILD = pathlib.Path(__file__).parent / "build"


def _broker(bench, ch32rv) -> str | None:
    """The endpoint of ch32rv's broker for the bench's port, or None when none runs."""
    import json
    out = subprocess.run([str(ch32rv), "broker", "endpoint", "--probe", f"port:{bench.port}", "--json"],
                         capture_output=True, text=True, timeout=30)
    try:
        return json.loads(out.stdout).get("result", {}).get("endpoint")
    except json.JSONDecodeError:
        return None


def test_tcp_link(dut, bench, ch32rv):
    if not bench.probe.get("wifi"):
        pytest.skip(f"{bench.name}: the probe joins no network (no wifi in the bench file)")
    kit.start(dut, "tcp_link")
    target = benchdef.tcp_target(bench, ch32rv)
    assert target, f"the probe is not on its network: wifi {benchdef.wifi_state(bench, ch32rv)}"
    print(f"[tcp] {target}")
    from oep_client import core, fixture, link, riscv
    from oep_client.host import Locked
    usual, close = benchdef.open_probe(bench, ch32rv)
    try:
        unit = benchdef.oep_probe_facts(usual).get("unit")
    finally:
        close()
    # The dut's monitor reads the slot's console through ch32rv's broker, which holds the probe's one lock for it:
    # the TCP session below needs that lock, so the monitor goes first and its broker with it.
    dut.serial.close()                                 # the arduino-cli monitor (its pyserial port) and its broker client
    gone = time.monotonic() + 15
    while _broker(bench, ch32rv) and time.monotonic() < gone:
        time.sleep(0.5)
    assert not _broker(bench, ch32rv), "ch32rv's broker is still up after the monitor closed"
    t = link.open_host(target)
    try:
        assert benchdef.oep_probe_facts(t).get("unit") == unit, "the TCP endpoint is another probe"
        # A broker that went down may leave its lock until its lease runs out (ch32rv 2247153 does not end its
        # session on the way down): wait that out, and say how long it took.
        t0 = time.monotonic()
        while True:
            try:
                t.open(lease_ms=10000, owner="tcp_link")
                break
            except Locked:
                if time.monotonic() - t0 > 15:
                    raise
                time.sleep(0.5)
        print(f"[tcp] session open after {time.monotonic() - t0:.1f} s")
        _wire, conn = benchdef.attach_slot(bench, t)
        dm = riscv.RiscvDm(t, conn)
        dm.halt()
        try:
            word = dm.read_block(0x08000000, 1)    # the reset vector's first instruction: flash, never blank
        finally:
            dm.resume()
        print(f"[tcp] riscv-dm: flash[0] = {word.hex()}")
        assert word not in (b"\xff\xff\xff\xff", b"\x00\x00\x00\x00"), f"flash[0] reads {word.hex()}"
        gfn = core.find_all(t, "oep.fixture.gpio")[0]
        pad = next(iter(bench.wiring))
        ch = int(bench.wiring[pad])
        core.plan_apply(t, [(gfn, 1, ch)])
        try:
            level = fixture.Gpio(t, gfn).read([ch])
        finally:
            core.plan_release(t, [gfn])
        print(f"[tcp] gpio: {pad} (channel {ch}) reads {level}")
        usual, close = benchdef.open_probe(bench, ch32rv)
        try:
            with pytest.raises(Locked):
                usual.open(lease_ms=3000, owner="tcp_link usual")
        finally:
            close()
        t.end()
    finally:
        t.link.close()
    probe = "tcp:" + target[len("tcp://"):]
    elf = next(BUILD.glob("*/tcp_link.ino.elf"), None)
    assert elf, f"no build of the sketch under {BUILD}"
    r = subprocess.run([str(ch32rv), "--probe", probe, "--chip", bench.data["dut"]["board"].lower(), "flash", str(elf)],
                       capture_output=True, text=True, timeout=120)
    print(f"[tcp] ch32rv flash: {(r.stdout + r.stderr).strip().splitlines()[-1] if (r.stdout + r.stderr).strip() else r.returncode}")
    assert r.returncode == 0, f"ch32rv flash over {probe} failed: {r.stderr.strip()}"
    m = subprocess.Popen([str(ch32rv), "--probe", probe, "--chip", bench.data["dut"]["board"].lower(), "monitor"],
                         stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    time.sleep(5)
    m.terminate()
    out, err = m.communicate(timeout=10)
    banners = out.decode(errors="replace").count("tcp_link READY")
    print(f"[tcp] ch32rv monitor: {banners} banner(s); stderr {err.decode(errors='replace').strip()[:200]}")
    assert banners, "ch32rv monitor over TCP heard no banner"
