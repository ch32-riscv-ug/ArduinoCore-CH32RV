"""Reset and start-up timing on the wire. The sketch raises a marker pad first thing in setup() and drops it right
before CH32.restart(); the probe's capture watches the marker, so the low time is restart -> setup() for a
software reset and ndmreset -> setup() for the probe's debug reset. resetReason() is read after each kind.

The marker pad is the bench file's facts.pwm, compiled in (bench/conftest.py -> TEST_BENCH_MARK_PIN ->
build_config.toml), since it has to be set before the console exists.

  uv run --env-file .env pytest bench/trace/reset_probe --profile ch32x035 -s
"""
import re
import time

import pytest
from loader import load

kit = load("tests/bench/bench_kit.py", "bench_kit")
tk = load("tests/bench/tracekit.py", "tracekit")

REPEAT = 5


def low_pulse(samples: bytes, bit: int = 0) -> tuple[int, int]:
    """(start, end) of the longest low run on `bit`, in samples."""
    best, start = (0, 0), None
    for i, b in enumerate(samples):
        lvl = (b >> bit) & 1
        if not lvl and start is None:
            start = i
        if lvl and start is not None:
            if i - start > best[1] - best[0]:
                best = (start, i)
            start = None
    if start is not None and len(samples) - start > best[1] - best[0]:
        best = (start, len(samples))
    return best


def test_reset_to_setup(request, dut, bench, ws_run):
    mark = bench.facts.get("pwm")
    if not mark:
        pytest.skip(f"{bench.name} names no marker pad (facts.pwm)")
    cap_max = int(bench.facts.get("capture_max_hz", 0))
    tk.need(bench, mark, capture_hz=400_000)
    rate = 1_000_000 if cap_max >= 5_000_000 else 400_000   # the slower sampler gets the longer window it needs
    kit.start(dut, "reset_probe")
    con = tk.Console(dut, ws_run)
    marked = con.ask("MARK", "MARK ")
    assert marked.endswith(str(tk.encode(mark))), f"the sketch marks pin {marked}, the bench says {mark}"
    oep_host = request.getfixturevalue("oep_host")
    fx = tk.Fixture(oep_host.host, bench, ws_run, con)
    try:
        cap = tk.Capture(fx)
        gpio = tk.Gpio(fx)
        fx.plan(cap.assignments(mark))
        gpio.configure(mark, tk.Gpio.INPUT_PULL_DOWN)   # a debug reset leaves the pad floating until setup()

        def reason() -> str:
            con.drain(0.02)
            m = re.search(r"REASON (\w+)", con.ask("REASON", "REASON ", 3))
            return m.group(1) if m else "?"

        def measure(kind: str, kick) -> float:
            cap.configure(rate, 130_816)
            con.drain(0.05)
            cap.arm()
            time.sleep(0.02)
            kick()
            con.wait("reset_probe READY", 5)
            st = cap.wait(2.0)
            data = cap.read_all() if st.flags & cap.COMPLETE else b""
            assert data, f"no capture for the {kind} reset"
            a, b = low_pulse(data, 0)
            ms = (b - a) / rate * 1000
            print(f"[{kind} reset] marker low {ms:.3f} ms; ends at {b / rate * 1000:.1f} ms of {len(data) / rate * 1000:.0f}")
            return ms

        print(f"[after upload (debug reset)] reset_reason={reason()}")
        sw = []
        with ws_run.section(1, "software reset"):
            for _ in range(REPEAT):
                sw.append(measure("software", lambda: con.send("REBOOT")))
        sw_reason = reason()
        print(f"[software reset] reset_reason={sw_reason}")
        from oep_client import target
        wire = target.Wire(fx.host, "oep.wire." + bench.data["slot"]["wire"])
        conn, _ = wire.attach(halt=False)
        dm = target.RiscvDm(fx.host, conn)
        dbg = []
        with ws_run.section(1, "debug reset"):
            for _ in range(REPEAT):
                dbg.append(measure("debug", lambda: dm.reset(confirm=False)))
        dbg_reason = reason()
        print(f"[debug reset] reset_reason={dbg_reason}")
        for name, v in (("software", sw), ("debug", dbg)):
            v = sorted(v)
            line = f"{name} reset -> setup(): median {v[len(v) // 2]:.3f} ms, min {v[0]:.3f}, max {v[-1]:.3f} (n={len(v)})"
            print(line)
            ws_run.note(line)
        assert sw_reason == "software", f"resetReason() after CH32.restart() is {sw_reason}"
        assert all(0.05 <= t <= 50 for t in sw), f"software reset -> setup() out of range: {sw}"
    finally:
        fx.release()
