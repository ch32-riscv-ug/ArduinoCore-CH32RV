"""Peripheral outputs on the wire: analogWrite PWM frequency and duty, tone(), delayMicroseconds() / millis()
timing, and SPI master mode / clock / data - captured by the OEP probe's oep.fixture.logic, decoded here, and recorded
into the test's WireSkein run, which pytest-embedded-wireskein checks against the expectations each section states.

The sketch drives whichever pins the host names (PINS), so the bench file decides the pads: facts.pwm and facts.spi.
A bench without a capture, or without those pads wired, skips.

One test per section. Each begins by syncing with the sketch and ends by releasing its plans, so any one runs alone.

  uv run --env-file .env pytest bench/trace/periph_probe --profile ch32x035 -s
"""
import random
import time

import pytest
from loader import load

kit = load("tests-legacy/bench/bench_kit.py", "bench_kit")
tk = load("tests-legacy/bench/tracekit.py", "tracekit")
from wireskein import runlog as ws  # noqa: E402

RATE = 2_000_000
PRETRIGGER = 1000                   # samples kept before an edge trigger
# The SPI captures have no trigger to lean on (the P4's capture offers only "immediate") and the probe holds
# 130816 samples over four channels: a 26 ms window at 5 MS/s, 13 ms at 10 MS/s. The host arms and then sends
# the command, and the command's way to the DUT (arduino-cli monitor -> ch32rv -> broker -> probe) takes 13..26 ms,
# so only the 5 MS/s window catches the transfer. That gives the decoder five samples per SCK period up to a
# 1 MHz SCK; faster transfers are captured for the record but not decoded until the probe has an edge trigger.
SPI_SAMPLES = 130_816
SPI_DECODE_MAX_HZ = 1_000_000


def spi_rate(hz: int, cap_max: int) -> int:
    return min(20_000_000 if hz > SPI_DECODE_MAX_HZ else 5_000_000, cap_max)


def spi_transfer(fx, cap, hz: int, mode: int, payload: bytes, rate: int) -> tuple[str, bytes]:
    """Arm, ask for a transfer, read the reply and the capture. -> (the got= line, the samples)."""
    con = fx.console
    if cap:
        cap.configure(rate, SPI_SAMPLES)
    con.drain(0.05)
    if cap:
        cap.arm()
    line = con.ask(f"SPI {hz} {mode} {payload.hex()}", "SPI got=")
    data = b""
    if cap:
        st = cap.wait(3.0)
        data = cap.read_all() if st.flags & cap.COMPLETE else b""
    return line, data
PAYLOAD, ANSWER = bytes.fromhex("a55a0f01"), bytes.fromhex("3c96c30f")


@pytest.fixture
def fx(request, dut, bench, ws_run):
    """The probe planned for this test, the sketch synced and told its pins."""
    tk.need(bench, capture_hz=RATE)
    facts = bench.facts
    pwm, spi = facts.get("pwm"), facts.get("spi", {})
    if not pwm or not spi:
        pytest.skip(f"{bench.name} declares no facts.pwm / facts.spi")
    kit.start(dut, "periph_probe")
    con = tk.Console(dut, ws_run)
    con.ask(f"PINS {tk.encode(pwm)} {tk.encode(spi['cs'])}", "PINS ok")
    # After the sketch answered: the broker oep_host joins is the one the dut's monitor started, and the
    # monitor is only surely up once something has been read through it.
    oep_host = request.getfixturevalue("oep_host")
    f = tk.Fixture(oep_host.host, bench, ws_run, con)
    f.pwm, f.spi = pwm, spi
    yield f
    f.release()


def spi_pads(fx) -> list[str]:
    return [fx.spi["sck"], fx.spi["mosi"], fx.spi["miso"], fx.spi["cs"]]


def spi_clock(fx, hz: float) -> float | None:
    """The core's SPI clock: PCLK / 2^k, the largest not above the request."""
    pclk = fx.spi.get("pclk_hz")
    if not pclk:
        return None
    div = 2
    while pclk / div > hz and div < 256:
        div *= 2
    return pclk / div


def spi_expect(fx, hz, mode, mosi, miso=None):
    return [ws.spi(*spi_pads(fx), mode=mode, mosi_bytes=mosi.hex(), miso_bytes=miso.hex() if miso else None,
                   hz=spi_clock(fx, hz)), ws.only_moving(spi_pads(fx))]


# ---------------------------------------------------------------- PWM / tone / timing on one pad

def test_pwm(fx, ws_run):
    cap = tk.Capture(fx)
    fx.plan(cap.assignments(fx.pwm))
    for duty in (64, 128, 192, 255, 0):
        want = [ws.square(fx.pwm, 1000, duty / 255, tol_freq=0.02, tol_duty=0.01) if 0 < duty < 255
                else ws.level(fx.pwm, 1 if duty == 255 else 0)]
        with ws_run.section(1, f"duty={duty}", expect=want):
            data = cap.grab(RATE, 40_000, f"PWM {duty}", "PWM duty=")
        st = tk.square_stats(data, 0, RATE)
        if st:
            print(f"[pwm duty={duty:3d}] f={st['freq_hz']:.1f} Hz duty={st['duty'] * 100:.1f}% "
                  f"(expected {duty / 255 * 100:.1f}%) jitter={st['period_jitter_us']:.2f} us")
        else:
            level = sum(d & 1 for d in data) / len(data) if data else None
            print(f"[pwm duty={duty:3d}] no edges, high fraction={level}")
        assert data, f"no capture for duty {duty}"


def test_tone(fx, ws_run):
    cap = tk.Capture(fx)
    fx.plan(cap.assignments(fx.pwm))
    for hz in (500, 1000, 4000):
        with ws_run.section(1, f"{hz}Hz", expect=[ws.square(fx.pwm, hz, 0.5, tol_freq=0.02, tol_duty=0.02)]):
            data = cap.grab(RATE, 60_000, f"TONE {hz}", "TONE hz=")
        st = tk.square_stats(data, 0, RATE)
        print(f"[tone {hz} Hz] f={st['freq_hz']:.1f} Hz duty={st['duty'] * 100:.1f}%" if st else f"[tone {hz}] no edges")
        assert data, f"no capture for {hz} Hz"
    with ws_run.section(1, "notone", expect=[ws.level(fx.pwm, 0)]):
        cap.grab(RATE, 1000, "NOTONE", "NOTONE")


def test_timing(fx, ws_run):
    cap = tk.Capture(fx)
    fx.plan(cap.assignments(fx.pwm))
    con = fx.console
    max_pre = cap.edge_trigger
    # Start at the pad's first edge where the probe offers an edge trigger (the classic ESP32: its immediate window
    # holds the command until it ends). Without one, the immediate window has to outlast the command's way in.
    edge = dict(edge=(fx.pwm, "both"), pretrigger=min(PRETRIGGER, max_pre)) if max_pre is not None else {}

    def toggled(command: str, done: str, rate: int, samples: int) -> bytes:
        cap.configure(rate, samples, **edge)
        con.drain(0.05)
        cap.arm()
        con.ask(command, done)
        st = cap.wait(3.0)
        return cap.read_all() if st.flags & cap.COMPLETE else b""

    # The command reaches the DUT 13..26 ms after the arm (see the SPI note), so an immediate window is the probe's
    # largest, 130816 samples: 65 ms at 2 MS/s, 130 ms at 1 MS/s for the 40 ms the 1000 us toggles take.
    for us in (0, 10, 100, 1000):
        rate = RATE if us < 1000 else 1_000_000
        with ws_run.section(1, f"toggle delay={us}us", expect=[ws.pulses(fx.pwm, count=20)]):
            data = toggled(f"TOGGLE {us} 20", "TOGGLE done", rate, 130_816)
        s = tk.square_stats(data, 0, rate)
        print(f"[delayMicroseconds({us})] half period {s['period_us'] / 2:.2f} us (edges {s['edges']})" if s
              else f"[delayMicroseconds({us})] no edges")
    with ws_run.section(1, "toggle no delay", expect=[ws.pulses(fx.pwm, count=20)]):
        data = toggled("TOGGLE0 20", "TOGGLE0 done", RATE, 130_816)
    s = tk.square_stats(data, 0, RATE)
    print(f"[digitalWrite pair] half period {s['period_us'] / 2:.2f} us" if s else "[digitalWrite pair] no edges")
    # As many 10 ms toggles as the window holds, starting low: half as many rising edges 20 ms apart, low at the end.
    cap.configure(1_000_000, 400_000, **edge)   # PARLIO: not below ~650 kHz
    if cap.config.samples / cap.rate < 0.25:
        cap.configure(400_000, 400_000, **edge)
    window = cap.config.samples / cap.rate
    toggles = min(20, 2 * int((window - (0.005 if edge else 0.05)) / 0.020))   # 50 ms for the command's way in
    with ws_run.section(1, "millis 10ms", expect=[ws.pulses(fx.pwm, count=toggles // 2, period_s=0.020, tol_period=0.01),
                                                  ws.ends({fx.pwm: 0})]):
        con.drain(0.05)
        cap.arm()
        line = con.ask(f"MILLIS 10 {toggles}", "MILLIS done")
        st = cap.wait(3.0)
        data = cap.read_all() if st.flags & cap.COMPLETE else b""
    s = tk.square_stats(data, 0, cap.rate)
    print(f"[millis 10 ms] period {s['period_us'] / 1000:.3f} ms (expected 20.000), DUT: {line}" if s
          else f"[millis] no edges: {line}")
    assert data, "no capture for the millis toggles"


# ---------------------------------------------------------------- SPI, alone and against the probe's SPI target

def test_spi(fx, ws_run, bench):
    tk.need(bench, *spi_pads(fx), capture_hz=5_000_000)   # the wire decode needs >= 5 samples per SCK period
    cap_max = int(bench.facts["capture_max_hz"])
    cap = tk.Capture(fx)
    fx.plan(cap.assignments(*spi_pads(fx)))
    # The sketch's first SPI command runs SPI.begin() and pinMode(CS): warm up without a capture, so the first
    # measured transfer is an ordinary one.
    spi_transfer(fx, None, 1_000_000, 0, PAYLOAD, 0)
    bad = []
    for hz, mode in ((1_000_000, 0), (1_000_000, 1), (1_000_000, 2), (1_000_000, 3), (4_000_000, 0), (250_000, 0),
                     (4_000_000, 3), (250_000, 2), (1_000_000, 0)):
        rate = spi_rate(hz, cap_max)
        wire = hz <= SPI_DECODE_MAX_HZ
        with ws_run.section(1, f"{hz}Hz mode{mode}", expect=spi_expect(fx, hz, mode, PAYLOAD) if wire else None):
            line, data = spi_transfer(fx, cap, hz, mode, PAYLOAD, rate)        # MISO: nobody answers
        if not wire:
            print(f"[spi {hz} Hz mode {mode}] captured, not decoded (needs an edge trigger) | {line}")
            continue
        d = tk.decode_spi(data, rate)
        cpol, cpha = (mode >> 1) & 1, mode & 1
        leading, trailing = ("rise", "fall") if cpol == 0 else ("fall", "rise")
        change_expected = leading if cpha else trailing     # CPHA=0 changes on the trailing edge
        near = d["mosi_changes_near"]
        change_seen = "rise" if near["rise"] > near["fall"] else "fall"
        data_ok = PAYLOAD in (d["mosi_rise"], d["mosi_fall"])
        ok = d["cpol"] == cpol and change_seen == change_expected and data_ok
        print(f"[spi {hz} Hz mode {mode}] CPOL={d['cpol']} (expect {cpol}), MOSI changes on {change_seen} "
              f"(expect {change_expected}), data={'ok' if data_ok else 'BAD'}, SCK {d['sck_hz'] / 1e6:.3f} MHz | {line}")
        if not ok:
            bad.append((hz, mode))
    assert not bad, f"SPI mode / data wrong on the wire for {bad}"


def first_diff(got: bytes, want: bytes) -> str:
    """Where two byte strings part: "ok", or the first differing offset with a few bytes of each side."""
    if got == want:
        return "ok"
    i = next((k for k in range(min(len(got), len(want))) if got[k] != want[k]), min(len(got), len(want)))
    return f"{len(got)} B, differs at {i}: got {got[i:i + 6].hex() or '-'} want {want[i:i + 6].hex()}"


def test_spi_peer(fx, ws_run, bench):
    """The probe's SPI target answers on MISO while the capture watches all four lines: what the DUT got, what the
    target received, and the wire decode all have to agree."""
    from oep_client.fixture import SpiTarget
    from oep_client.host import Unsupported
    tk.need(bench, *spi_pads(fx))
    cap_max = int(bench.facts.get("capture_max_hz", 0))
    decode_ok = cap_max >= 5_000_000
    spi = SpiTarget(fx.host)
    cap = tk.Capture(fx) if cap_max else None
    pads = spi_pads(fx)
    fx.plan(spi.assignments(sck=fx.channel(pads[0]), mosi=fx.channel(pads[1]), miso=fx.channel(pads[2]),
                            cs=fx.channel(pads[3])) + (cap.assignments(*pads) if cap else []))
    # Warm-up before arming: the DUT's first SPI command runs SPI.begin() and pinMode(CS); with CS floating until
    # then the target sees a spurious frame that would consume the armed transaction.
    # A target that needs time between CS and the first clock declares it (describe cs_setup_ns; the classic ESP32's
    # CS gate, 15 us): the DUT keeps it, as a real master would.
    lead_us = -(-spi.cs_setup_ns // 1000)
    fx.console.ask(f"CSLEAD {lead_us}", "CSLEAD us=")
    if lead_us:
        print(f"[spi peer] the target declares cs_setup_ns={spi.cs_setup_ns}: the DUT waits {lead_us} us after CS")
    spi.configure(0)
    spi_transfer(fx, None, 1_000_000, 0, PAYLOAD, 0)
    time.sleep(0.05)
    bad = []
    # Only clocks the target declares it follows (describe max_clock_hz): above that a wrong bit is the probe's,
    # not the DUT's. A target that declares nothing gets the whole list.
    declared = spi.max_clock_hz
    top = declared or 24_000_000
    cases = [(hz, mode) for hz, mode in ((1_000_000, 0), (1_000_000, 1), (1_000_000, 2), (1_000_000, 3), (250_000, 0),
                                         (4_000_000, 0), (4_000_000, 3), (12_000_000, 0), (12_000_000, 3),
                                         (24_000_000, 0)) if hz <= top]
    print(f"[spi peer] the target declares max_clock_hz={declared}")
    # A mode the target does not offer is refused (unsupported; the classic ESP32's takes modes 0 and 2 only): that
    # mode is the probe's limit, not a DUT failure, so its cases are skipped. Mode 0 every target has to take.
    refused = set()
    for hz, mode in cases:
        if mode in refused:
            continue
        try:
            spi.configure(mode)
        except Unsupported as e:
            refused.add(mode)
            print(f"[spi peer] the target refuses mode {mode} ({e}): its cases skipped")
            ws_run.note(f"spi target refuses mode {mode}")
            continue
        spi.arm(len(PAYLOAD), ANSWER)
        rate = spi_rate(hz, cap_max or 1)
        wire = decode_ok and hz <= SPI_DECODE_MAX_HZ
        with ws_run.section(1, f"{hz}Hz mode{mode}", expect=spi_expect(fx, hz, mode, PAYLOAD, ANSWER) if wire else None):
            line, data = spi_transfer(fx, cap, hz, mode, PAYLOAD, rate)
        got = bytes.fromhex(line.split("got=")[1]) if "got=" in line else b""
        time.sleep(0.05)
        pending, bits, rx = spi.read_rx()
        d = tk.decode_spi(data, rate)
        cpha, cpol = mode & 1, (mode >> 1) & 1
        sample_edge = ("fall" if cpol == 0 else "rise") if cpha else ("rise" if cpol == 0 else "fall")
        wire_miso, wire_mosi = d["miso_" + sample_edge], d["mosi_" + sample_edge]
        wire_ok = (wire_miso == ANSWER and wire_mosi == PAYLOAD) if wire else True
        ok = got == ANSWER and rx == PAYLOAD and bits == len(PAYLOAD) * 8 and wire_ok
        print(f"[spi peer {hz} Hz mode {mode}] DUT got={got.hex()} target rx={rx.hex()} bits={bits} "
              f"wire MISO={wire_miso.hex()} MOSI={wire_mosi.hex()} -> {'OK' if ok else 'BAD'}")
        if not ok:
            bad.append((hz, mode))
    # 64-byte transactions back to back, and a burst of five 4-byte ones with one arm each (the sketch caps a
    # transfer at 64 bytes).
    rnd = random.Random(7)
    long_payload = bytes(rnd.randrange(256) for _ in range(64))
    long_answer = bytes(rnd.randrange(256) for _ in range(64))
    for n in range(3):
        spi.configure(0)
        spi.arm(64, long_answer)
        line, _ = spi_transfer(fx, None, 1_000_000, 0, long_payload, 0)
        got = bytes.fromhex(line.split("got=")[1]) if "got=" in line else b""
        time.sleep(0.05)
        pending, bits, rx = spi.read_rx()
        st = spi.status()                     # one frame per arm: a target that restarted mid-frame counts more
        ok = got == long_answer and rx == long_payload and bits == 512
        print(f"[spi peer 64-byte #{n}] {'OK' if ok else 'BAD'} bits={bits} target transactions={st.transactions} "
              f"errors={st.errors}"
              + ("" if ok else f" | DUT got {first_diff(got, long_answer)} | target rx {first_diff(rx, long_payload)}"))
        if not ok:
            bad.append(("64-byte", n))
    burst_mode = 0 if 3 in refused else 3
    spi.configure(burst_mode)
    burst_ok = 0
    burst_hz = min(4_000_000, top)
    for n in range(5):
        spi.arm(4, ANSWER)
        line, _ = spi_transfer(fx, None, burst_hz, burst_mode, PAYLOAD, 0)
        got = bytes.fromhex(line.split("got=")[1]) if "got=" in line else b""
        pending, bits, rx = spi.read_rx()
        burst_ok += got == ANSWER and rx == PAYLOAD
    print(f"[spi peer burst, 5 x 4-byte at {burst_hz / 1e6:g} MHz mode {burst_mode}] {burst_ok}/5 both ways")
    assert 0 not in refused, "the SPI target refuses mode 0"
    assert burst_ok == 5 and not bad, f"SPI peer mismatches: {bad}, burst {burst_ok}/5"
