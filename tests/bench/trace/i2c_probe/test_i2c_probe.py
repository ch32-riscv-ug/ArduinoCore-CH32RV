"""Wire as a master, watched on the wire. The DUT talks to the probe's I2C target over the bench's route while the
probe's capture samples SCL / SDA in the same plan; the host decodes START / bytes / ACK / STOP next to what Wire
reported, and the run's expectations (ws.i2c) are checked by pytest-embedded-wireskein at teardown.

The route and its pads are the bench file's facts.i2c; the sketch is told the pads (PINS) for the stuck-bus part.
One test per scenario; each syncs with the sketch and plans what it needs.

  uv run --env-file .env pytest bench/trace/i2c_probe --profile ch32x035 -s
"""
import time

import pytest
from loader import load

kit = load("tests/bench/bench_kit.py", "bench_kit")
tk = load("tests/bench/tracekit.py", "tracekit")
from wireskein import runlog as ws  # noqa: E402

ADDRESS = 0x42
SAMPLES = 130_816          # the probe's largest capture over two channels; the command's way in takes 13..26 ms


def rate_for(hz: int, cap_max: int) -> int:
    """Six samples per SCL period at least, within the largest window: 1 MS/s (130 ms) up to 100 kHz, 2.5 MS/s
    (52 ms) for 400 kHz."""
    return min(2_500_000 if hz >= 400_000 else 1_000_000, cap_max)


@pytest.fixture
def fx(request, dut, bench, ws_run):
    i2c = bench.facts.get("i2c")
    if not i2c:
        pytest.skip(f"{bench.name} declares no facts.i2c")
    tk.need(bench, i2c["scl"], i2c["sda"], capture_hz=1_000_000)
    kit.start(dut, "i2c_probe")
    con = tk.Console(dut, ws_run)
    con.ask(f"PINS {tk.encode(i2c['scl'])} {tk.encode(i2c['sda'])}", "PINS ok")
    oep_host = request.getfixturevalue("oep_host")
    f = tk.Fixture(oep_host.host, bench, ws_run, con)
    f.scl, f.sda, f.route = i2c["scl"], i2c["sda"], int(i2c["route"])
    f.cap_max = int(bench.facts.get("capture_max_hz", 0))
    from oep_client.fixture import I2cTarget
    f.target = I2cTarget(f.host)
    f.cap = tk.Capture(f)
    f.plan(f.target.assignments(sda=f.channel(f.sda), scl=f.channel(f.scl)) + f.cap.assignments(f.scl, f.sda))
    yield f
    f.release()


def expect_i2c(fx, transactions, hz=None, released=True):
    """The bus as the test means it: (addr, rw, bytes, ack) transactions, SCL at hz, both lines high at the end."""
    return [ws.i2c(fx.scl, fx.sda, [{"addr": a, "rw": rw, "bytes": list(b), "ack": ack} for a, rw, b, ack in transactions],
                   hz=hz, released=released)]


def traced(fx, command: str, reply: str, hz: int):
    """Arm the capture, send the command, wait its reply, read the capture. -> (reply line, I2cTrace, samples)."""
    rate = rate_for(hz, fx.cap_max)
    fx.cap.configure(rate, SAMPLES)
    fx.console.drain(0.05)
    fx.cap.arm()
    line = fx.console.ask(command, reply, 5)
    st = fx.cap.wait(3.0)
    samples = fx.cap.read_all() if st.flags & fx.cap.COMPLETE else b""
    return line, tk.decode_i2c(samples), samples


def begin(fx):
    fx.console.ask(f"BEGIN {fx.route}", "BEGIN route=", 5)
    time.sleep(0.2)


def field(line: str, key: str) -> str:
    return line.split(key)[1].split()[0] if key in line else ""


# ---------------------------------------------------------------- writes

def test_i2c_write(fx, ws_run):
    """Each clock: two writes to the target (the first after a settled bus, then a repeat), then to nobody (address
    NACK, rc 2). The target's receive buffer has to hold the bytes; the wire has to show them acknowledged."""
    T = fx.target
    begin(fx)
    bad = []
    case = 0
    for hz in (100_000, 10_000):
        for address, listening in ((ADDRESS, True), (ADDRESS, True), (ADDRESS + 1, False)):
            case += 1
            payload = bytes((0x10 * case + i) & 0xFF for i in range(4))     # distinct per case: stale data shows
            T.configure(ADDRESS, T.MODE_FIXED_RX)
            T.arm_rx(len(payload))
            want = expect_i2c(fx, [(address, "write", payload if listening else b"", listening)], hz)
            with ws_run.section(1, f"case{case} {hz}Hz addr={address:02x}", expect=want):
                line, trace, _ = traced(fx, f"WRITE {fx.route} {hz} {address:02x} {payload.hex()}", "WRITE rc=", hz)
            time.sleep(0.05)
            pending, data = T.read_rx()
            rc = int(field(line, "rc="))
            ok = (rc == 0 and data == payload) if listening else (rc == 2)
            print(f"[{hz} Hz -> 0x{address:02x}{' (target)' if listening else ' (nobody)'}] {line} | wire {trace.summary()} "
                  f"| target rx={data.hex() or '-'} -> {'OK' if ok else 'BAD'}")
            if not ok:
                bad.append((hz, address))
    assert not bad, f"Wire write wrong for {bad}"


def test_i2c_read_and_repeated_start(fx, ws_run):
    """READ from the target's preloaded 4-byte slots; write two bytes without STOP then read four (a repeated
    START); a 400 kHz write."""
    T = fx.target
    begin(fx)
    T.configure(ADDRESS, T.MODE_PRELOADED_TX)
    slots = [bytes.fromhex("a1b2c3d4"), bytes.fromhex("11223344")]
    for s in slots:
        T.preload_tx(s)
    bad = []
    for n, want in enumerate(slots):
        with ws_run.section(1, f"read slot{n}", expect=expect_i2c(fx, [(ADDRESS, "read", want, True)], 100_000)):
            line, trace, _ = traced(fx, f"READ {fx.route} 100000 {ADDRESS:02x} 4", "READ got=", 100_000)
        got = bytes.fromhex(field(line, "data=") or "")
        print(f"[read slot{n}] {line} | wire {trace.summary()} | expected {want.hex()} -> {'OK' if got == want else 'BAD'}")
        if got != want:
            bad.append(f"read slot{n}")
    T.configure(ADDRESS, T.MODE_PRELOADED_TX)
    T.preload_tx(bytes.fromhex("55667788"))
    with ws_run.section(1, "write-then-read", expect=expect_i2c(fx, [(ADDRESS, "write", b"\x01\x02", True),
                                                                      (ADDRESS, "read", bytes.fromhex("55667788"), True)], 100_000)):
        line, trace, _ = traced(fx, f"WRREAD {fx.route} 100000 {ADDRESS:02x} 0102", "WRREAD rc=", 100_000)
    ok = field(line, "rc=") == "0" and field(line, "data=").lower() == "55667788"
    print(f"[write-then-read, repeated START] {line} | wire {trace.summary()} -> {'OK' if ok else 'BAD'}")
    if not ok:
        bad.append("write-then-read")
    if fx.cap_max >= 2_500_000:
        T.configure(ADDRESS, T.MODE_FIXED_RX)
        T.arm_rx(4)
        with ws_run.section(1, "write 400kHz", expect=expect_i2c(fx, [(ADDRESS, "write", bytes.fromhex("0a0b0c0d"), True)], 400_000)):
            line, trace, _ = traced(fx, f"WRITE {fx.route} 400000 {ADDRESS:02x} 0a0b0c0d", "WRITE rc=", 400_000)
        time.sleep(0.05)
        pending, rx = T.read_rx()
        rate = rate_for(400_000, fx.cap_max)
        period = sorted(trace.scl_periods)[len(trace.scl_periods) // 2] / rate * 1e6 if trace.scl_periods else 0
        ok = field(line, "rc=") == "0" and rx.hex() == "0a0b0c0d"
        print(f"[write 400 kHz] {line} | wire {trace.summary()} | SCL period {period:.2f} us | target rx={rx.hex()} -> {'OK' if ok else 'BAD'}")
        if not ok:
            bad.append("write 400kHz")
    assert not bad, f"Wire read paths wrong: {bad}"


def test_i2c_clock_switch(fx, ws_run):
    """Wire.setClock() between transactions, no Wire.begin() again: up and down, each transaction at its clock.
    400 kHz only where the capture has six samples a period."""
    T = fx.target
    begin(fx)
    steps = [hz for hz in (100_000, 400_000, 10_000, 400_000, 100_000, 10_000) if rate_for(hz, fx.cap_max) >= 6 * hz]
    bad = []
    for k, hz in enumerate(steps):
        payload = bytes((0xA0 + 4 * k + i) & 0xFF for i in range(4))
        T.configure(ADDRESS, T.MODE_FIXED_RX)
        T.arm_rx(len(payload))
        with ws_run.section(1, f"step{k} {hz}Hz", expect=expect_i2c(fx, [(ADDRESS, "write", payload, True)], hz)):
            line, trace, _ = traced(fx, f"WRITE {fx.route} {hz} {ADDRESS:02x} {payload.hex()}", "WRITE rc=", hz)
        time.sleep(0.05)
        pending, rx = T.read_rx()
        ok = rx == payload and field(line, "rc=") == "0"
        print(f"[clock switch {k}: {hz} Hz] {line} | wire {trace.summary()} | target rx={rx.hex() or '-'} -> {'OK' if ok else 'BAD'}")
        if not ok:
            bad.append((k, hz))
    assert not bad, f"setClock() steps wrong: {bad}"


# ---------------------------------------------------------------- the target misbehaving

def test_i2c_clock_stretch(fx, ws_run):
    """The target holds every stretch for stretch_us (the ESP32 slave stretches at address match for a master READ).
    Below Wire's 25 ms timeout (CH32_WIRE_TIMEOUT_US) the read completes with the slot's bytes; above it Wire gives
    up within the timeout, and the next read with the stretch off works again."""
    T = fx.target
    begin(fx)
    slot = "a1b2c3d4"
    bad = []

    def read4():
        line, trace, samples = traced(fx, f"READ {fx.route} 100000 {ADDRESS:02x} 4", "READ got=", 100_000)
        return int(field(line, "got=")), field(line, "data=").lower(), int(field(line, "t_us=")), trace, samples

    for stretch_us in (0, 100, 1000, 5000, 20000, 30000):
        T.stretch(stretch_us)
        T.configure(ADDRESS, T.MODE_PRELOADED_TX)
        T.preload_tx(bytes.fromhex(slot))
        with ws_run.section(1, f"stretch {stretch_us}us"):
            got, data, t_us, trace, samples = read4()
        rate = rate_for(100_000, fx.cap_max)
        low = max((len(run) for run in bytes(b & 1 for b in samples).split(b"\x01")), default=0) / rate * 1e6
        expect_ok = stretch_us < 25_000
        ok = (got == 4 and data == slot) if expect_ok else (got == 0 and t_us < 40_000)
        print(f"[stretch {stretch_us:6d} us, READ 4] got={got} data={data or '-'} t={t_us / 1000:.2f} ms "
              f"max SCL low {low / 1000:.2f} ms | wire {trace.summary()} -> {'OK' if ok else 'BAD'}")
        if not ok:
            bad.append(stretch_us)
    T.stretch(0)
    T.configure(ADDRESS, T.MODE_PRELOADED_TX)
    T.preload_tx(bytes.fromhex(slot))
    with ws_run.section(1, "after stretch, stretch off"):
        got, data, t_us, trace, _ = read4()
    ok = got == 4 and data == slot
    print(f"[after the timeout, stretch off, READ 4] got={got} data={data or '-'} t={t_us / 1000:.2f} ms -> {'OK' if ok else 'BAD'}")
    if not ok:
        bad.append("after")
    assert not bad, f"clock stretching handled wrong at {bad}"


def test_i2c_stuck_bus(fx, ws_run):
    """A line held low by something else (the probe's GPIO): Wire gives up with an error and, once released, an
    address NACK (rc 2, nobody listening). Then a real target left driving SDA low mid-byte: Wire fails, Wire.clearBus()
    frees the bus, and the next write is acknowledged."""
    T = fx.target
    G = tk.Gpio
    begin(fx)
    bad = []
    # Part 1: the target leaves the plan so the pins are free for the probe's GPIO to hold.
    fx.release()
    fx.plan(fx.cap.assignments(fx.scl, fx.sda))
    gpio = tk.Gpio(fx)

    def write2():
        line, trace, _ = traced(fx, f"WRITE {fx.route} 100000 {ADDRESS:02x} 0102", "WRITE rc=", 100_000)
        return int(field(line, "rc=")), int(field(line, "t_us=")), line, trace

    for name, pad in (("SDA", fx.sda), ("SCL", fx.scl)):
        gpio.configure(pad, G.OUTPUT_LOW)
        time.sleep(0.01)
        with ws_run.section(1, f"{name} held low"):          # the fault is the probe's: recorded, the check is that Wire gives up
            rc, t_us, line, trace = write2()
        print(f"[{name} held low by the probe] rc={rc} t={t_us / 1000:.2f} ms | wire {trace.summary()}")
        if rc == 0:
            bad.append(f"{name} held low: rc 0")
        with ws_run.section(1, f"{name} held low, 2nd try"):
            rc2, t2, _, _ = write2()
        print(f"[{name} still low, 2nd try] rc={rc2} t={t2 / 1000:.2f} ms")
        gpio.configure(pad, G.INPUT_FLOATING)
        time.sleep(0.01)
        with ws_run.section(1, f"{name} released, nobody", expect=expect_i2c(fx, [(ADDRESS, "write", b"", False)], 100_000)):
            rc3, t3, _, trace3 = write2()
        print(f"[{name} released, nobody listening] rc={rc3} t={t3 / 1000:.2f} ms | wire {trace3.summary()} -> {'OK' if rc3 == 2 else 'BAD'}")
        if rc3 != 2:
            bad.append(f"{name} released: rc {rc3}")
    # Part 2: a real target left driving SDA low in the middle of a byte.
    fx.release()
    fx.plan(T.assignments(sda=fx.channel(fx.sda), scl=fx.channel(fx.scl)) + fx.cap.assignments(fx.scl, fx.sda))
    fx.console.drain(0.3)
    T.stretch(0)
    T.configure(ADDRESS, T.MODE_PRELOADED_TX)
    T.preload_tx(bytes(4))
    with ws_run.section(1, "target left mid-byte"):          # the fault itself: recorded, not checked
        stuck_line, trace, _ = traced(fx, f"STUCK {ADDRESS:02x}", "STUCK ack=", 100_000)
    print(f"[target left mid-byte] {stuck_line} | wire {trace.summary()}")
    begin(fx)
    for attempt in (1, 2):
        with ws_run.section(1, f"write, SDA held by target, try {attempt}"):
            rc, t_us, line, trace = write2()
        print(f"[Wire.begin + write, SDA held by the target, try {attempt}] rc={rc} t={t_us / 1000:.2f} ms | wire {trace.summary()}")
        if rc == 0:
            bad.append(f"stuck write {attempt}: rc 0")
    fx.console.drain(0.05)
    clr = fx.console.ask("BUSCLR", "BUSCLR free=", 5)
    print(f"[bus clear from the sketch] {clr}")
    begin(fx)
    T.configure(ADDRESS, T.MODE_FIXED_RX)
    T.arm_rx(2)
    with ws_run.section(1, "after bus clear", expect=expect_i2c(fx, [(ADDRESS, "write", b"\x01\x02", True)], 100_000)):
        rc, t_us, line, trace = write2()
    time.sleep(0.05)
    pending, rx = T.read_rx()
    ok = rc == 0 and rx.hex() == "0102"
    print(f"[after bus clear] rc={rc} t={t_us / 1000:.2f} ms | wire {trace.summary()} | target rx={rx.hex() or '-'} -> {'OK' if ok else 'BAD'}")
    if not ok:
        bad.append("after bus clear")
    assert field(clr, "free=") == "1" and not bad, f"stuck bus handled wrong: {bad}, clearBus {clr}"
