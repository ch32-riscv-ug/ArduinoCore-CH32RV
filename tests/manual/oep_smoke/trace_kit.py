"""OEP v1 parts in the shapes the peripheral trace tests use (oep_adc_trace, oep_gpio_matrix, oep_i2c_trace,
oep_periph_trace, oep_reset_trace, oep_uart_trace). They were written against the v0 client; this keeps their test
logic as it was and does the work through oep_client.v1:

- Session: build a sketch, program it through the host's CH32 flash knowledge, open its console, reset it running.
- plans are per fn (oep-core §8): Session.plan() applies the fns it names and leaves the others; Gpio adds a channel
  to its own plan the first time it drives or reads it (v0 took no plan for gpio).
- Capture: one-shot captures handed back as one byte per sample, bit k = channel k of the plan (the v0 layout the
  tests decode), whatever sample width the probe chose.
- Run: with --run-dir, a WireSkein run (wireskein/prototype/wsproto/runlog.py): the heading markers the test opens,
  the console lines sent and received, and every capture with its DUT pad names; `ws verify` then checks the
  captures against the expectations the test gave each heading (wireskein docs/capture-test-guide.ja.md).
"""
from __future__ import annotations

import contextlib
import os
import pathlib
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass

import oep_smoke
import targets

DEFAULT_WIRESKEIN = pathlib.Path.home() / "dev_oep" / "wireskein" / "prototype"


def add_run_arguments(parser) -> None:
    parser.add_argument("--run-dir", help="record a WireSkein run here (markers, console lines, captures, "
                        "expectations) and check it with ws verify; the exit code follows the verdict")
    parser.add_argument("--wireskein", default=str(DEFAULT_WIRESKEIN), help="WireSkein prototype directory")


class NoRun:
    """The recorder's calls, doing nothing: a test without --run-dir runs as before."""

    def heading(self, level: int, name: str = "") -> None:
        pass

    @contextlib.contextmanager
    def section(self, level: int, name: str, expect: list | None = None, **rules):
        yield self

    def command(self, text: str) -> None:
        pass

    def reply(self, text: str) -> None:
        pass

    def note(self, text: str) -> None:
        pass

    def armed(self) -> float:
        return 0.0

    def capture(self, *args, **kw) -> str:
        return ""


class Run:
    """A WireSkein run: the recorder (runlog.Recorder, standard library only), its expectation helpers, and
    `ws verify` at the end. Without --run-dir the recorder is NoRun and the helpers still build their dicts."""

    def __init__(self, args, profile: dict, **meta):
        sys.path.insert(0, args.wireskein)
        from wsproto import runlog
        self.runlog = runlog
        self.dir = pathlib.Path(args.run_dir) if args.run_dir else None
        self.wireskein = pathlib.Path(args.wireskein)
        self.rec = runlog.Recorder(self.dir, target=args.target, fqbn=profile["fqbn"], **meta) if self.dir else NoRun()

    def __getattr__(self, name):   # square, level, ends, i2c, spi, pulses ...: runlog's helpers
        return getattr(self.runlog, name)

    def verify(self, log=print) -> int:
        """Close the run and check it: -> ws verify's exit code (0 when nothing is NG; 0 without --run-dir)."""
        if self.dir is None:
            return 0
        self.rec.close()
        size = sum(f.stat().st_size for f in self.dir.iterdir() if f.suffix in (".json", ".bin"))
        log(f"run {self.dir}: {len(self.rec.doc['captures'])} captures, {size} bytes (run.json + .bin)")
        cmd = ["uv", "run", "--project", str(self.wireskein), "python", "ws.py", "verify", str(self.dir.resolve()),
               "--junit", str((self.dir / "report.xml").resolve()), "--json", str((self.dir / "report.json").resolve())]
        return subprocess.run(cmd, cwd=self.wireskein, env={**os.environ, "PYTHONPATH": "."}).returncode


class LineLink(oep_smoke.Link):
    """The console as oep_smoke.Link, but wait() returns only once the line holding the match is complete: the v1
    console (dmseq) arrives a few bytes at a time, and the tests parse the rest of that line right after waiting.
    Lines sent and the lines waited for go to the run's log."""

    rec = NoRun()

    def send(self, line: str) -> None:
        for text in line.splitlines():
            if text.strip():
                self.rec.command(text.strip())
        super().send(line)

    def wait(self, text: str, timeout: float, regex: bool = False) -> bool:
        deadline = time.monotonic() + timeout
        if not super().wait(text, timeout, regex):
            self.rec.note(f"no reply {text!r} in {timeout} s")
            return False
        while "\n" not in self.text[self.cursor:]:
            if time.monotonic() >= deadline:
                break
            self.pump()
            time.sleep(0.005)
        start = self.text.rfind("\n", 0, self.cursor) + 1
        end = self.text.find("\n", self.cursor)
        self.rec.reply(self.text[start:end if end >= 0 else None].strip())
        return True


class Session:
    """One probe session with the sketch built, programmed and running, and its console open."""

    def __init__(self, port: str, profile: dict, name: str, log, *, source: pathlib.Path | None = None,
                 fqbn: str | None = None, defines: list[str] | None = None, run: Run | None = None):
        from oep_client.v1 import target
        self.log = log
        self.profile = profile
        self.rec = run.rec if run is not None else NoRun()
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
        self.link.rec = self.rec
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
    OPEN_DRAIN_LOW, OPEN_DRAIN_RELEASE, INPUT_PULL_UP_DOWN = 5, 6, 7

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
    """oep.fixture.capture one-shot, handed back one byte per sample (bit k = channel k). Each capture read goes to
    the run with the DUT pad names of its channels and the time it was armed."""
    COMPLETE = 1

    def __init__(self, session: Session):
        from oep_client.v1 import capture
        self.session = session
        self.capture = capture.LogicCapture(session.host)
        self.fn = self.capture.fn
        self._mod = capture
        self.channels = 0
        self.lines: tuple[int, ...] = ()
        self.config = None
        self.rate = 0
        self._armed = 0.0

    def assignments(self, *channels: int) -> list[tuple[int, int, int]]:
        self.channels = len(channels)
        self.lines = channels
        return [(self.fn, k, ch) for k, ch in enumerate(channels)]

    def configure(self, rate: int, samples: int) -> None:
        self.config = self.capture.configure(rate=rate, mode=self._mod.ONE_SHOT, samples=samples)
        self.rate = float(self.config.rate) if self.config.rate else rate   # the rate the probe chose

    def arm(self) -> None:
        self.capture.start()
        self._armed = self.session.rec.armed()

    def wait(self, timeout: float) -> CaptureStatus:
        try:
            segments = self.capture.wait(timeout)
        except Exception:   # noqa: BLE001 - not done in time: an incomplete status, as v0 reported it
            return CaptureStatus(0, 0)
        self._segment = segments[0] if segments else None
        return CaptureStatus(self._segment.samples if self._segment else 0, self.COMPLETE if self._segment else 0)

    def read_all(self, samples: int) -> bytes:
        if not getattr(self, "_segment", None):
            self.session.rec.note("capture incomplete")
            return b""
        data = self.capture.read_segment(self._segment)
        lines = [self.capture.channel(data, k, samples) for k in range(self.channels)]
        out = bytes(sum(line[i] << k for k, line in enumerate(lines)) for i in range(samples))
        if out:
            extra = {"time_base_slipped": True} if self._segment.slipped else {}   # the probe saw its pacing slip
            self.session.rec.capture(out, self.rate, [targets.pin_name(self.session.profile, ch) for ch in self.lines],
                                     self._armed, start_us=self._segment.start_us, **extra)
        return out


class Uart:
    """oep.fixture.uart as a byte stream (configure, read, write) with the v0 assignments()."""

    def __init__(self, session: Session, fn: int | None = None):
        from oep_client.v1 import fixture, target
        self.fn = fn if fn is not None else target.find_all(session.host, "oep.fixture.uart")[0]
        self.io = fixture.FixtureUartIO(session.host, self.fn)

    def assignments(self, rx: int, tx: int) -> list[tuple[int, int, int]]:
        return [(self.fn, 1, rx), (self.fn, 2, tx)]

    def configure(self, baud: int, fmt: int | None = None) -> int:
        """fmt: fixture.FixtureUart.format_byte(); None is 8N1."""
        return self.io.configure(baud, fmt)

    def read(self, n: int = 4096) -> bytes:
        return self.io.read(n)

    def write(self, data: bytes) -> int:
        return self.io.write(data)


def decode_i2c(samples: bytes, scl_bit: int = 0, sda_bit: int = 1):
    """The v1 decoder on one-byte-per-sample captures."""
    from oep_client.v1 import decode
    return decode.decode_i2c([(b >> scl_bit) & 1 for b in samples], [(b >> sda_bit) & 1 for b in samples])
