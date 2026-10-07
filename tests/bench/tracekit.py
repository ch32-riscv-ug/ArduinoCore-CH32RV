"""The OEP fixture, as the trace tests use it: plans, one-shot captures recorded into the test's WireSkein run,
fixture GPIO and UART, and the console as a command / reply pair that the run logs.

A trace test gets `dut` (the sketch's Console over the platform monitor), `oep_host` (a session on the probe
through ch32rv's broker), `bench` (tests/benches/<name>.toml) and `ws_run` (pytest-embedded-wireskein's recorder),
and builds a Fixture from them. Pads are named the DUT's way ("PA1"); the bench file says which probe channel that
is, and a pad the bench does not wire skips the test with that reason. Nothing here knows a jig.

Loaded with `load("tests/bench/tracekit.py", "tracekit")` (tests/loader.py).
"""
from __future__ import annotations

import re
import statistics
import time
from dataclasses import dataclass

import pytest

PORTS = "ABCDEF"
PIN_PORT_BITS = 5      # cores/arduino/ch32rv_pins.h, ADR-0010
PIN_PORT_FIRST = 2     # PA's value in the port field: PA0 = 64


def encode(pad: str) -> int:
    """"PA0" / "PC19" -> the core's pin number, ((port + 2) << 5) | bit; a plain integer passes through."""
    pad = pad.strip().upper()
    if re.fullmatch(r"\d+", pad):
        return int(pad)
    m = re.fullmatch(r"P([A-F])(\d{1,2})", pad)
    if not m:
        raise ValueError(f"{pad!r} is not a pad name like PA0 or PC19")
    return ((PORTS.index(m.group(1)) + PIN_PORT_FIRST) << PIN_PORT_BITS) | int(m.group(2))


def need(bench, *pads: str, capture_hz: int = 0) -> None:
    """Skip when the bench does not wire these pads, or its capture is too slow (0: none)."""
    missing = [p for p in pads if not isinstance(bench.wiring.get(p), int)]
    if missing:
        pytest.skip(f"{bench.name} does not wire {', '.join(missing)} to the probe")
    if capture_hz and int(bench.facts.get("capture_max_hz", 0)) < capture_hz:
        pytest.skip(f"{bench.name} has no oep.fixture.logic of {capture_hz / 1e6:g} MS/s or more")


class Console:
    """The DUT's console as command -> reply line, both written to the run's log."""

    def __init__(self, dut, ws_run):
        self.dut = dut
        self.ws = ws_run

    def send(self, command: str) -> None:
        self.ws.command(command)
        self.dut.write(command + "\n")

    def ask(self, command: str, reply: str, timeout: float = 5.0) -> str:
        """Send `command`, wait for the line holding `reply` (the whole line, since the console arrives a few
        bytes at a time), and return it stripped."""
        self.send(command)
        return self.wait(reply, timeout)

    def wait(self, reply: str, timeout: float = 5.0) -> str:
        m = self.dut.expect(re.compile(rb"[^\r\n]*" + re.escape(reply.encode()) + rb"[^\r\n]*\r?\n"), timeout=timeout)
        line = m.group(0).decode("utf-8", "replace").strip()
        self.ws.reply(line)
        return line

    def drain(self, seconds: float = 0.05) -> None:
        """Let the banner and stragglers go by, so the next wait cannot match an old line."""
        import pexpect
        try:
            self.dut.expect(pexpect.TIMEOUT, timeout=seconds)
        except Exception:   # noqa: BLE001 - nothing to drain is fine
            pass


class Fixture:
    """Plans on the probe for the test's duration, released at teardown. Sub-fixtures are built from it."""

    def __init__(self, host, bench, ws_run, console: Console):
        self.host = host
        self.bench = bench
        self.ws = ws_run
        self.console = console
        self.planned: set[int] = set()

    def plan(self, assignments: list[tuple[int, int, int]]) -> list[int]:
        """Apply these (fn, role, channel); the fns named are replaced, the others kept."""
        from oep_client import core
        core.plan_apply(self.host, assignments)
        fns = sorted({fn for fn, _, _ in assignments})
        self.planned.update(fns)
        return fns

    def release(self, fns: list[int] | None = None) -> None:
        """Release the plans this test applied - only those. plan_release with no fns drops every plan on the
        probe, including the saved one that gives the fixture UART its pins (oep.probe.config), and the next
        bench run then fails its check."""
        from oep_client import core
        fns = sorted(fns or self.planned)
        if not fns:
            return
        try:
            core.plan_release(self.host, fns)
        except Exception:   # noqa: BLE001 - a session already gone has nothing to release
            return
        self.planned.difference_update(fns)

    def channel(self, pad: str) -> int:
        return self.bench.channel(pad)


@dataclass
class CaptureStatus:
    samples: int
    flags: int


class Capture:
    """oep.fixture.logic one-shot, handed back one byte per sample (bit k = the k-th pad given to assignments()).
    Every capture read goes to the run with its pad names and the time it was armed."""
    COMPLETE = 1

    def __init__(self, fx: Fixture):
        from oep_client import capture
        self.fx = fx
        self.capture = capture.LogicCapture(fx.host)
        self.fn = self.capture.fn
        self._mod = capture
        self.pads: tuple[str, ...] = ()
        self.config = None
        self.rate = 0.0
        self._armed = 0.0
        self._segment = None

    def assignments(self, *pads: str) -> list[tuple[int, int, int]]:
        self.pads = pads
        return [(self.fn, k, self.fx.channel(p)) for k, p in enumerate(pads)]

    EDGES = {"rise": 0, "fall": 1, "both": 2}
    TAG_TRIGGER = 0x45                             # describe: trigger types(u32 bit set) max_pretrigger(u32)

    @property
    def edge_trigger(self) -> int | None:
        """max_pretrigger when the probe's capture offers an edge trigger (describe 0x45, oep-if-capture), else None
        (immediate only)."""
        import struct
        from oep_client import core
        for tag, value in core.describe(self.fx.host, self.fn):
            if tag & 0x7F == self.TAG_TRIGGER and len(value) >= 8:
                types, max_pre = struct.unpack_from("<II", value)
                return max_pre if types & (1 << self._mod.EDGE) else None
        return None

    def configure(self, rate: int, samples: int, edge: tuple[str, str] | None = None, pretrigger: int = 0) -> None:
        """edge = (pad, "rise" / "fall" / "both"): the capture starts at that edge (pretrigger samples kept before it)
        instead of at the arm. A DUT that acts after the arm needs it on the classic ESP32, where an immediate window
        holds every SWIO request and console byte until it ends (oep-probe-arduino implementation-limits).
        A probe that cannot trigger at this rate / size (rejected unsupported, or unavailable as the P4's PARLIO at 400000 samples
        answers today) gets
        the immediate window instead, noted in the run; on the classic that window then shows as missing edges."""
        from oep_client.host import Unavailable, Unsupported
        trigger = None
        if edge is not None:
            pad, kind = edge
            trigger = (self._mod.EDGE, self.pads.index(pad), self.EDGES[kind])
        try:
            self.config = self.capture.configure(rate=rate, mode=self._mod.ONE_SHOT, samples=samples, trigger=trigger,
                                                 pretrigger=pretrigger if trigger else None)
        except (Unavailable, Unsupported) as e:
            if trigger is None:
                raise
            self.fx.ws.note(f"edge trigger unavailable at {rate} Hz x {samples} ({e}): immediate window")
            self.config = self.capture.configure(rate=rate, mode=self._mod.ONE_SHOT, samples=samples)
        self.rate = float(self.config.rate) if self.config.rate else float(rate)

    def arm(self) -> None:
        self.capture.start()
        self._armed = self.fx.ws.armed()

    def wait(self, timeout: float = 3.0) -> CaptureStatus:
        try:
            segments = self.capture.wait(timeout)
        except Exception:   # noqa: BLE001 - not done in time: an incomplete status
            self._segment = None
            return CaptureStatus(0, 0)
        self._segment = segments[0] if segments else None
        return CaptureStatus(self._segment.samples if self._segment else 0, self.COMPLETE if self._segment else 0)

    def read_all(self) -> bytes:
        """The finished capture as bytes (bit k = pad k), recorded; b"" when it did not complete."""
        if self._segment is None:
            self.fx.ws.note("capture incomplete")
            return b""
        samples = self._segment.samples
        data = self.capture.read_segment(self._segment)
        lines = [self.capture.channel(data, k, samples) for k in range(len(self.pads))]
        out = bytes(sum(line[i] << k for k, line in enumerate(lines)) for i in range(samples))
        if out:
            extra = {"time_base_slipped": True} if self._segment.slipped else {}
            # The segment's start on the probe's clock (oep-if-capture §2 since oep-probe-arduino 0.0.10:
            # start_ns with its uncertainty; it was start_us before).
            self.fx.ws.capture(self._armed, self.rate, interleaved=out, names=list(self.pads),
                               start_ns=self._segment.start_ns,
                               start_uncertainty_ns=self._segment.start_uncertainty_ns, **extra)
        return out

    def grab(self, rate: int, samples: int, command: str | None = None, reply: str | None = None,
             settle: float = 0.05, timeout: float = 3.0) -> bytes:
        """Configure, (send a command and wait its reply,) settle, arm, wait, read."""
        self.configure(rate, samples)
        con = self.fx.console
        con.drain(0.05)
        if command:
            con.ask(command, reply or command.split()[0], 5)
        time.sleep(settle)
        self.arm()
        st = self.wait(timeout)
        return self.read_all() if st.flags & self.COMPLETE else b""


class Gpio:
    """oep.fixture.gpio by pad name. The fn's plan holds the pads in use: a pad joins it the first time it is
    driven or read, and `only()` narrows the plan back to the pads a test is working on (a plan of 16 channels was
    refused as malformed on the P4, 2026-09-30)."""
    INPUT_FLOATING, INPUT_PULL_UP, INPUT_PULL_DOWN, OUTPUT_LOW, OUTPUT_HIGH = 0, 1, 2, 3, 4
    OPEN_DRAIN_LOW, OPEN_DRAIN_RELEASE = 5, 6

    def __init__(self, fx: Fixture):
        from oep_client import core, fixture
        self.fx = fx
        self.fn = core.find_all(fx.host, "oep.fixture.gpio")[0]
        self.gpio = fixture.Gpio(fx.host, self.fn)
        self.channels: set[int] = set()

    def _own(self, channel: int) -> None:
        if self.fn not in self.fx.planned:
            self.channels.clear()
        if channel not in self.channels:
            self.channels.add(channel)
            self.fx.plan([(self.fn, 1, c) for c in sorted(self.channels)])

    def only(self, *pads: str) -> None:
        """Plan exactly these pads (the rest leave the plan and go back to idle)."""
        self.channels = {self.fx.channel(p) for p in pads}
        self.fx.plan([(self.fn, 1, c) for c in sorted(self.channels)])

    def configure(self, pad: str, mode: int) -> None:
        ch = self.fx.channel(pad)
        self._own(ch)
        self.gpio.set([(ch, mode)])

    def read(self, pad: str) -> int:
        ch = self.fx.channel(pad)
        self._own(ch)
        return self.gpio.read([ch])[0]


class Uart:
    """oep.fixture.uart as a byte stream. The bench's fixture UART already has its pins from the probe's saved
    plan (oep.probe.config, written by prepare.py), so this takes the first oep.fixture.uart that accepts a
    configure - the one with pins - the way the ch32_uart fixture does."""

    def __init__(self, fx: Fixture, baud: int = 115200, fmt: int | None = None):
        from oep_client import core, fixture, host as oh
        self.fx = fx
        last = None
        for fn in core.find_all(fx.host, "oep.fixture.uart"):
            io = fixture.FixtureUartIO(fx.host, fn)
            try:
                io.configure(baud, fmt)
            except oh.Rejected as e:
                last = e
                continue
            self.fn, self.io = fn, io
            return
        pytest.skip(f"no oep.fixture.uart on this probe has pins for the DUT's UART ({last}): prepare.py writes the plan")

    def configure(self, baud: int, fmt: int | None = None) -> int:
        return self.io.configure(baud, fmt)

    def collect(self, n: int, timeout: float) -> bytes:
        """Up to n bytes within timeout seconds."""
        buf, t0 = bytearray(), time.perf_counter()
        while len(buf) < n and time.perf_counter() - t0 < timeout:
            chunk = self.io.read(1024)
            if chunk:
                buf += chunk
        return bytes(buf)

    def flush_input(self) -> None:
        while self.io.read(4096):
            pass

    def read(self, n: int = 4096) -> bytes:
        return self.io.read(n)

    def write(self, data: bytes) -> int:
        return self.io.write(data)


# ---------------------------------------------------------------- decoding one-byte-per-sample captures

def edges(samples: bytes, bit: int) -> list[tuple[int, int]]:
    prev = (samples[0] >> bit) & 1
    out = []
    for i in range(1, len(samples)):
        cur = (samples[i] >> bit) & 1
        if cur != prev:
            out.append((i, cur))
            prev = cur
    return out


def square_stats(samples: bytes, bit: int, rate: float) -> dict | None:
    """period / duty statistics of a square wave from its edges; None with fewer than three rising edges."""
    if not samples:
        return None
    e = edges(samples, bit)
    rises = [i for i, lvl in e if lvl == 1]
    falls = [i for i, lvl in e if lvl == 0]
    if len(rises) < 3:
        return None
    periods = [b - a for a, b in zip(rises, rises[1:])]
    highs = [f - r for r in rises for f in [next((x for x in falls if x > r), None)] if f is not None]
    period = statistics.median(periods)
    high = statistics.median(highs) if highs else 0
    return {"edges": len(e), "period_us": period / rate * 1e6, "freq_hz": rate / period, "duty": high / period,
            "period_jitter_us": (max(periods) - min(periods)) / rate * 1e6}


def decode_spi(samples: bytes, rate: float) -> dict:
    """One CS-framed SPI transfer on a capture planned as (SCK, MOSI, MISO, CS) = bits 0..3: the bytes seen on
    each SCK edge, the SCK idle level before CS fell (CPOL), the SCK frequency, the CS-low time, and on which SCK
    edge MOSI changes."""
    empty = {"mosi_rise": b"", "mosi_fall": b"", "miso_rise": b"", "miso_fall": b"", "cpol": None, "sck_hz": 0.0,
             "mosi_changes_near": {"rise": 0, "fall": 0}, "clocks": 0, "cs_low_us": None}
    if not samples:
        return empty
    prev = samples[0]
    bits = {"rise": [], "fall": []}
    miso_bits = {"rise": [], "fall": []}
    sck_rises = []
    cs_fall = cs_rise = None
    for i in range(1, len(samples)):
        cur = samples[i]
        cs_prev, cs_cur = (prev >> 3) & 1, (cur >> 3) & 1
        if cs_prev and not cs_cur and cs_fall is None:
            cs_fall = i
        if not cs_prev and cs_cur and cs_fall is not None and cs_rise is None:
            cs_rise = i
        if cs_fall is not None and cs_rise is None:
            sck_prev, sck_cur = prev & 1, cur & 1
            if sck_prev != sck_cur:
                edge = "rise" if sck_cur else "fall"
                bits[edge].append((cur >> 1) & 1)
                miso_bits[edge].append((cur >> 2) & 1)
                if sck_cur:
                    sck_rises.append(i)
        prev = cur

    def pack(b):
        return bytes(int("".join(map(str, b[k:k + 8])), 2) for k in range(0, len(b) - len(b) % 8, 8))

    sck_hz = rate / statistics.median([b - a for a, b in zip(sck_rises, sck_rises[1:])]) if len(sck_rises) > 2 else 0.0
    prev = samples[0]
    sck_edges, mosi_changes = [], []
    for i in range(1, len(samples)):
        cur = samples[i]
        if cs_fall is not None and (cs_rise is None or i < cs_rise) and i > cs_fall:
            if (prev ^ cur) & 1:
                sck_edges.append((i, "rise" if cur & 1 else "fall"))
            if (prev ^ cur) & 2:
                mosi_changes.append(i)
        prev = cur
    near = {"rise": 0, "fall": 0}
    for m in mosi_changes:
        if sck_edges:
            near[min(sck_edges, key=lambda x: abs(x[0] - m))[1]] += 1
    idle = (samples[cs_fall - 1] & 1) if cs_fall else None
    return {"mosi_rise": pack(bits["rise"]), "mosi_fall": pack(bits["fall"]),
            "miso_rise": pack(miso_bits["rise"]), "miso_fall": pack(miso_bits["fall"]),
            "cpol": idle, "sck_hz": sck_hz, "mosi_changes_near": near, "clocks": len(sck_rises),
            "cs_low_us": ((cs_rise or len(samples)) - cs_fall) / rate * 1e6 if cs_fall is not None else None}


def decode_i2c(samples: bytes, scl_bit: int = 0, sda_bit: int = 1):
    """oep_client's I2C decoder on a capture planned as (SCL, SDA) = bits 0, 1. -> I2cTrace (events, scl_periods)."""
    from oep_client import decode
    return decode.decode_i2c([(b >> scl_bit) & 1 for b in samples], [(b >> sda_bit) & 1 for b in samples])
