"""OEP v1 parts in the shapes the peripheral trace tests use (oep_adc_trace, oep_gpio_matrix, oep_i2c_trace,
oep_periph_trace, oep_reset_trace, oep_uart_trace). They were written against the v0 client; this keeps their test
logic as it was and does the work through oep_client.v1:

- Session: build a sketch, program it through the host's CH32 flash knowledge, open its console, reset it running.
- plans are per fn (oep-core §8): Session.plan() applies the fns it names and leaves the others; Gpio adds a channel
  to its own plan the first time it drives or reads it (v0 took no plan for gpio).
- Capture: one-shot captures handed back as one byte per sample, bit k = channel k of the plan (the v0 layout the
  tests decode), whatever sample width the probe chose.
"""
from __future__ import annotations

import pathlib
import tempfile
import time
from dataclasses import dataclass

import oep_smoke


class LineLink(oep_smoke.Link):
    """The console as oep_smoke.Link, but wait() returns only once the line holding the match is complete: the v1
    console (dmseq) arrives a few bytes at a time, and the tests parse the rest of that line right after waiting."""

    def wait(self, text: str, timeout: float, regex: bool = False) -> bool:
        deadline = time.monotonic() + timeout
        if not super().wait(text, timeout, regex):
            return False
        while "\n" not in self.text[self.cursor:]:
            if time.monotonic() >= deadline:
                break
            self.pump()
            time.sleep(0.005)
        return True


class Session:
    """One probe session with the sketch built, programmed and running, and its console open."""

    def __init__(self, port: str, profile: dict, name: str, log, *, source: pathlib.Path | None = None,
                 fqbn: str | None = None, defines: list[str] | None = None):
        from oep_client.v1 import target
        self.log = log
        with tempfile.TemporaryDirectory() as tmp:
            kw = {"source": source} if source is not None else {}
            if defines:
                kw["defines"] = defines
            binary = oep_smoke.build(name, fqbn or profile["fqbn"], pathlib.Path(tmp), log, **kw)
            image = binary.read_bytes()
        self.bench = oep_smoke.Bench(port or profile["port"], profile)
        self.host = self.bench.host
        for attempt in range(5):
            try:
                self.outcome, self.conn, self.dm = oep_smoke.program(self.bench, image, profile, log)
                break
            except Exception as e:   # the CH32L103's flying leads: an attach now and then needs another try
                if attempt == 4:
                    raise
                log(f"program: {e}; trying again")
                time.sleep(0.3 * (attempt + 1))
        log(f"programmed {self.outcome.bytes} bytes (rewrote {self.outcome.rewritten_pages} pages) "
            f"verified={self.outcome.verified}")
        if not self.outcome.verified:
            raise SystemExit("program/verify failed")
        self.link = LineLink(oep_smoke.open_console(self.bench, self.conn).uart)
        self.reset()
        self._planned: set[int] = set()
        self.target = target

    def reset(self, confirm: bool = True) -> "Reset":
        """The debug reset (riscv-dm reset): -> flags (bit1 = seen running when confirm)."""
        flags, attempts, pc = self.dm.reset(confirm=confirm)
        return Reset(flags, attempts, pc)

    def plan(self, assignments: list[tuple[int, int, int]]) -> list[int]:
        """Apply these (fn, role, channel); the fns named are replaced, others kept. -> the fns."""
        from oep_client.v1 import core
        core.plan_apply(self.host, assignments)
        fns = sorted({fn for fn, _, _ in assignments})
        self._planned.update(fns)
        return fns

    def release(self, fns: list[int] | None = None) -> None:
        from oep_client.v1 import core
        core.plan_release(self.host, fns or [])
        if fns:
            self._planned.difference_update(fns)
        else:
            self._planned.clear()

    def close(self) -> None:
        """Plans off, the target reset running, the session ended."""
        try:
            self.release()
            self.reset()
        finally:
            self.bench.close()


@dataclass
class Reset:
    flags: int
    attempts: int
    pc: int


class Gpio:
    """oep.fixture.gpio with the v0 names; a channel joins this fn's plan the first time it is used."""
    INPUT_FLOATING, INPUT_PULL_UP, INPUT_PULL_DOWN, OUTPUT_LOW, OUTPUT_HIGH = 0, 1, 2, 3, 4
    OPEN_DRAIN_LOW, OPEN_DRAIN_RELEASE = 5, 6

    def __init__(self, session: Session):
        from oep_client.v1 import fixture, target
        self.session = session
        self.fn = target.find_all(session.host, "oep.fixture.gpio")[0]
        self.gpio = fixture.Gpio(session.host, self.fn)
        self.channels: set[int] = set()

    def _own(self, channel: int) -> None:
        if self.fn not in self.session._planned:   # the session released the plans: start over
            self.channels.clear()
        if channel not in self.channels:
            self.channels.add(channel)
            self.session.plan([(self.fn, 1, c) for c in sorted(self.channels)])

    def configure(self, channel: int, mode: int) -> None:
        self._own(channel)
        self.gpio.set([(channel, mode)])

    def read(self, channel: int) -> int:
        self._own(channel)
        return self.gpio.read([channel])[0]


@dataclass
class CaptureStatus:
    samples: int
    flags: int


class Capture:
    """oep.fixture.capture one-shot, handed back one byte per sample (bit k = channel k)."""
    COMPLETE = 1

    def __init__(self, session: Session):
        from oep_client.v1 import capture
        self.session = session
        self.capture = capture.LogicCapture(session.host)
        self.fn = self.capture.fn
        self._mod = capture
        self.channels = 0
        self.config = None

    def assignments(self, *channels: int) -> list[tuple[int, int, int]]:
        self.channels = len(channels)
        return [(self.fn, k, ch) for k, ch in enumerate(channels)]

    def configure(self, rate: int, samples: int) -> None:
        self.config = self.capture.configure(rate=rate, mode=self._mod.ONE_SHOT, samples=samples)

    def arm(self) -> None:
        self.capture.start()

    def wait(self, timeout: float) -> CaptureStatus:
        try:
            segments = self.capture.wait(timeout)
        except Exception:   # noqa: BLE001 - not done in time: an incomplete status, as v0 reported it
            return CaptureStatus(0, 0)
        self._segment = segments[0] if segments else None
        return CaptureStatus(self._segment.samples if self._segment else 0, self.COMPLETE if self._segment else 0)

    def read_all(self, samples: int) -> bytes:
        if not getattr(self, "_segment", None):
            return b""
        data = self.capture.read_segment(self._segment)
        lines = [self.capture.channel(data, k, samples) for k in range(self.channels)]
        return bytes(sum(line[i] << k for k, line in enumerate(lines)) for i in range(samples))


class Uart:
    """oep.fixture.uart as a byte stream (configure, read, write) with the v0 assignments()."""

    def __init__(self, session: Session, fn: int | None = None):
        from oep_client.v1 import fixture, target
        self.fn = fn if fn is not None else target.find_all(session.host, "oep.fixture.uart")[0]
        self.io = fixture.FixtureUartIO(session.host, self.fn)

    def assignments(self, rx: int, tx: int) -> list[tuple[int, int, int]]:
        return [(self.fn, 1, rx), (self.fn, 2, tx)]

    def configure(self, baud: int) -> int:
        return self.io.configure(baud)

    def read(self, n: int = 4096) -> bytes:
        return self.io.read(n)

    def write(self, data: bytes) -> int:
        return self.io.write(data)


def decode_i2c(samples: bytes, scl_bit: int = 0, sda_bit: int = 1):
    """The v1 decoder on one-byte-per-sample captures."""
    from oep_client.v1 import decode
    return decode.decode_i2c([(b >> scl_bit) & 1 for b in samples], [(b >> sda_bit) & 1 for b in samples])
