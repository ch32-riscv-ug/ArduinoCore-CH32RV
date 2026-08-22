#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.9"
# dependencies = ["pyserial>=3.5"]
# ///
"""Compile, flash and read back a tests/sketches/basic sketch on one board.

It compiles and uploads exactly the way a user would - `arduino-cli upload
--programmer wch-link`, which drives probe-rs - so a pass means the shipping
path works, not just the code.

  cd tests
  uv run pytest manual/smoke/smoke.py -v -s                  # one case per sketch
  CH32_SKETCH=all uv run pytest manual/smoke/smoke.py -v -s  # the whole set

  uv run tests/manual/smoke/smoke.py                   # the same, as a CLI
  uv run tests/manual/smoke/smoke.py --sketch all
  uv run tests/manual/smoke/smoke.py --board CH32X035  # assert which board

Both routes call resolve_bench() and run_one(), so they cannot disagree about
which board a given chip means. The CLI exists for interactive bench work; the
pytest cases are the entry point everywhere else, and take their settings from
the environment (see tests/.env.example).

--board is optional: probe-rs reports the exact part number and boards.txt maps
it back, so the board the image is built for cannot disagree with the board it
is flashed to. Pass it anyway when you want the run to *assert* which board it
tested - CI does, and the [compile only] series have no probe-rs target to
detect.

`--sketch all` is what to run after swapping a tier B board onto the bench
(see tests/TEST_PLAN.ja.md): one command, one summary table.

It speaks the command protocol (tests/sketches/testcmd.h): the sketch repeats
"<name> READY" twice a second and does nothing until asked, so however long the
flash took there is a banner to wait for. Then `PING <token>` has to come back
as `PONG <token>`. The token matters *here* and nowhere else - this is the one
caller that flashes sketches back to back, and every sketch answers PING, so a
PONG the previous one left in the probe's FIFO would answer for a board that is
not running. That is exactly the failure this runner used to have, scoring nine
sketches against the last one's output.

What counts as a pass comes from each sketch's own test_<name>.py, so adding a
sketch needs no change here. Each of those is one test function that writes
commands and reads answers in order - a script - and it is replayed as one:

  dut.write("RUN\n")             send it
  dut.expect_exact("...")        read until that literal arrives
  dut.expect(r"...")             the same, as a regex (PASS|SKIP)
  anything f-string              skipped; the test builds the value itself

A sketch whose test writes nothing is driven with the standard RUN. Two rules
apply on top, because the sketches decide pass/fail on the target:

  - the output must contain no "FAIL"
  - if it reports "failures=", that has to be "failures=0"

Nothing is skipped for driving the target any more: serial_echo and
hooks_selftest are replayed from their own test files rather than left to
pytest.

The probe and its UART bridge are discovered by USB VID:PID, because boards get
swapped on this bench and /dev/ttyACM* is not stable. Pass --probe <serial> when
more than one probe is attached, or --port to override the discovery.

The board's Serial pins are printed before flashing: wire those two to the
probe's UART bridge (TX -> probe RX, RX -> probe TX) first.

Tools come from <repo>/.tools, which `uv run tools/index/fetch_tools.py` fills.
CH32_GCC_BIN and CH32_PROBE_RS override that if a bench keeps them elsewhere.
"""
import argparse
import ast
import dataclasses
import itertools
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
import time

REPO = pathlib.Path(__file__).resolve().parents[3]
BASIC = REPO / "tests" / "sketches" / "basic"
DEFAULT_SKETCH = "serial_println"

# What to copy into a build directory, shared with the two compile harnesses so
# that a sketch gaining a file does not fail in three places one at a time.
sys.path.insert(0, str(REPO / "tests" / "sketches"))
from stage import stage_sketch                              # noqa: E402


class Failure(Exception):
    """The bench cannot do what was asked, and says why."""

# WCH-Link USB ids. RV mode is the one probe-rs drives; the same device also
# exposes a CDC UART bridge, so one cable carries both flashing and Serial.
WCH_LINK_VID = 0x1A86
WCH_LINK_PIDS = (0x8010, 0x8012)


def expectations(name: str) -> tuple:
    """The test file's own script: [("write"|"expect"|"match", text)], in order.

    Read out of test_<name>.py rather than restated here, so the pytest run and
    this one cannot disagree about what passing means - adding a sketch needs no
    change to this runner. Each file is one test function that writes commands
    and reads answers in order, which is exactly a script.

    Only plain literals are collected. An f-string means the test parametrises
    the value, and guessing what it expands to would be worse than admitting we
    cannot check it; the FAIL and failures= rules in run_one still cover those.
    """
    test = BASIC / name / f"test_{name}.py"
    if not test.exists():
        return ()
    steps = []
    for node in ast.walk(ast.parse(test.read_text(encoding="utf-8"))):
        if not isinstance(node, ast.Call) or not isinstance(node.func,
                                                            ast.Attribute):
            continue
        arg = node.args[0] if node.args else None
        if not isinstance(arg, ast.Constant) or not isinstance(arg.value, str):
            continue
        # expect() takes a regex and expect_exact() takes a literal; keeping
        # them apart is what lets the SKIP-tolerant checks be replayed at all.
        verb = {"write": "write", "expect_exact": "expect",
                "expect": "match"}.get(node.func.attr)
        if verb:
            steps.append((node.lineno, verb, arg.value))
    # ast.walk is breadth-first, and a script only means anything in order.
    steps.sort()
    # run_one has already waited for the banner, and it repeats, so replaying
    # that step would only cost half a second of waiting for the next one.
    return tuple((verb, text) for _, verb, text in steps
                 if not (verb == "expect" and text == f"{name} READY"))


def _find(buf: bytes, target: bytes, pos: int) -> int:
    """Index just past `target` at or after `pos`, or -1."""
    found = buf.find(target, pos)
    return -1 if found < 0 else found + len(target)


def show(text: str) -> None:
    """Print what the board said, whether the run passed or not.

    The READY banner repeats twice a second, so a run that waits for anything
    collects a column of them. They are folded into one line with a count: the
    fact that the board kept announcing itself is worth seeing, forty copies of
    it are not.
    """
    lines = []
    for line in text.strip().splitlines():
        if lines and lines[-1][0] == line:
            lines[-1][1] += 1
        else:
            lines.append([line, 1])
    print("--- output " + "-" * 48)
    print("\n".join(f"{line}   (x{n})" if n > 1 else line
                    for line, n in lines) or "(nothing received)")
    print("-" * 59)


class Link:
    """A serial port, plus everything read from it so far.

    pyserial has no expect(), and the one this needs is not quite pexpect's: a
    match has to advance a cursor so that replaying "write X, expect Y" twice
    does not match the first Y both times. Hence the explicit position.
    """

    def __init__(self, uart):
        self.uart = uart
        self.buf = b""
        self.pos = 0

    def send(self, text: str) -> None:
        self.uart.write(text.encode())
        self.uart.flush()

    def wait(self, needle: str, seconds: float) -> bool:
        """Read until `needle` appears after the last match, or time out."""
        return self._until(lambda buf, pos: _find(buf, needle.encode(), pos),
                           seconds)

    def match(self, pattern: str, seconds: float) -> bool:
        """The same, for a regex - what dut.expect() takes."""
        rx = re.compile(pattern.encode())
        def search(buf, pos):
            m = rx.search(buf, pos)
            return m.end() if m else -1
        return self._until(search, seconds)

    def _until(self, find, seconds: float) -> bool:
        deadline = time.time() + seconds
        while True:
            end = find(self.buf, self.pos)
            if end >= 0:
                self.pos = end
                return True
            if time.time() >= deadline:
                return False
            chunk = self.uart.read(256)
            if chunk:
                self.buf += chunk

    def drain(self, seconds: float) -> None:
        deadline = time.time() + seconds
        while time.time() < deadline:
            chunk = self.uart.read(256)
            if chunk:
                self.buf += chunk

    @property
    def text(self) -> str:
        return self.buf.decode(errors="replace")


# Tokens have to differ between runs as well as within one, because the stale
# bytes a run is trying to see past can be the previous run's.
_tokens = itertools.count(os.getpid() % 9000 + 1000)


def handshake(link: Link, attempts: int = 3, timeout: float = 5.0) -> bool:
    """PING <token> -> PONG <token>. True once the target has answered.

    The token is what makes this a proof rather than a guess: every sketch
    answers PING, so a bare PONG still in the FIFO would satisfy a bare PING.
    """
    for _ in range(attempts):
        token = next(_tokens)
        link.send(f"PING {token}\n")
        if link.wait(f"PONG {token}", timeout):
            return True
    return False


def sketch_names(which: str):
    if which != "all":
        return [which]
    return sorted(d.name for d in BASIC.iterdir()
                  if (d / f"{d.name}.ino").exists())


def boards_for(chip: str) -> dict:
    """{board id: [part numbers]} whose generated probe-rs chip name matches.

    Matching is on {build.probe_rs_chip} rather than on the name, so a part
    whose series id does not begin with its board id still resolves.
    """
    text = (REPO / "boards.txt").read_text(encoding="utf-8")
    hits = {}
    for line in text.splitlines():
        key, _, value = line.partition("=")
        if not key.endswith(".build.probe_rs_chip"):
            continue
        if value.strip().upper() != chip.upper():
            continue
        parts = key.split(".")
        pnum = parts[3] if len(parts) > 4 and parts[1] == "menu" else "ANY"
        hits.setdefault(parts[0], []).append(pnum)
    return hits


def find_probes():
    """[(serial, tty)] for every attached WCH-Link, newest enumeration last."""
    from serial.tools import list_ports
    found = []
    for port in sorted(list_ports.comports(), key=lambda p: p.device):
        if port.vid == WCH_LINK_VID and port.pid in WCH_LINK_PIDS:
            found.append((port.serial_number or "", port.device))
    return found


def resolve_port(want_serial, want_port):
    """Pick the probe's UART bridge. Returns (port, serial) or exits."""
    if want_port and want_serial:
        return want_port, want_serial
    probes = find_probes()
    if want_serial:
        for serial_number, tty in probes:
            if serial_number == want_serial:
                return want_port or tty, serial_number
        raise Failure(f"no WCH-Link with serial {want_serial}; attached: "
                      f"{[s for s, _ in probes] or 'none'}")
    if want_port:
        return want_port, None
    if not probes:
        raise Failure("no WCH-Link found (looked for "
                      f"{WCH_LINK_VID:04x}:{'/'.join('%04x' % p for p in WCH_LINK_PIDS)})")
    if len(probes) > 1:
        raise Failure("several WCH-Links attached; pick one with --probe: "
                      + ", ".join(f"{s} ({t})" for s, t in probes))
    return probes[0][1], probes[0][0]


def serial_pins(board: str, override=None):
    """Read the chosen USART and its pins out of the generated variant."""
    header = (REPO / "variants" / board / "pins_arduino.h").read_text(encoding="utf-8")
    if override is not None:
        n = str(override)
        if f"#define CH32_SERIAL{n}_TX" not in header:
            raise Failure(f"{board}: the variant has no USART{n}")
    else:
        index = re.search(r"#define CH32_SERIAL_DEFAULT (\d+)", header)
        if not index:
            return None
        n = index.group(1)
    tx = re.search(rf"#define CH32_SERIAL{n}_TX (\w+)", header)
    rx = re.search(rf"#define CH32_SERIAL{n}_RX (\w+)", header)
    note = re.search(rf"/\* USART{n}: ([^*]+)\*/", header)
    return n, tx.group(1), rx.group(1), (note.group(1).strip() if note else "")


BENCH = REPO / "tests" / "manual" / "bench.json"


def bench_serial(board: str):
    """The USART this bench has wired for a board, or None if unrecorded."""
    if not BENCH.exists():
        return None
    entry = json.loads(BENCH.read_text(encoding="utf-8"))["boards"].get(board)
    return entry.get("serial") if entry else None


def _holds_probe_rs(d):
    return (d / "probe-rs").exists() or (d / "probe-rs.exe").exists()


def find_probe_rs():
    """Where probe-rs is, wherever it came from.

    Three places, in order of how deliberate they are:
      1. CH32_PROBE_RS - an explicit override always wins.
      2. <repo>/.tools - what tools/index/fetch_tools.py puts in the project.
         This is the reproducible one: same path on every machine, which
         matters most on Windows where nothing else is predictable.
      3. ~/.arduino15/packages/... - a Board Manager install, i.e. a machine
         that already has the platform the way a user would.

    Not searched: arduino-cli's `internal/` profile cache. Its directory names
    carry a hash of the index URL, so they cannot be guessed - ask for it with
    `arduino-cli compile --profile <p> --show-properties` instead.
    """
    override = os.environ.get("CH32_PROBE_RS")
    if override:
        return override
    project = REPO / ".tools" / "probe-rs"
    installed = (pathlib.Path.home() / ".arduino15" / "packages"
                 / "ch32-riscv-ug" / "tools" / "probe-rs")
    for root in (project, installed):
        found = sorted(d for d in root.glob("*") if _holds_probe_rs(d))
        if found:
            return str(found[-1])
    return None


def find_tables():
    """The ch32-device-data tables: CH32_TABLES, else <repo>/.tools."""
    override = os.environ.get("CH32_TABLES")
    if override:
        return override
    d = REPO / ".tools" / "ch32-device-data" / "tables"
    return str(d) if d.is_dir() else None


def find_gcc_bin():
    """The toolchain bin directory: CH32_GCC_BIN, else <repo>/.tools."""
    override = os.environ.get("CH32_GCC_BIN")
    if override:
        return override
    root = REPO / ".tools" / "xpack-riscv-none-elf-gcc"
    found = sorted(d for d in root.glob("*") if (d / "bin").is_dir())
    return str(found[-1] / "bin") if found else None


def detected_chip(probe_rs_dir, probe_serial):
    """What probe-rs thinks is attached, or None if it cannot tell."""
    cmd = [str(pathlib.Path(probe_rs_dir) / "probe-rs"), "info"]
    if probe_serial:
        cmd += ["--probe", f"1a86:8010:{probe_serial}"]
    out = subprocess.run(cmd, capture_output=True, text=True).stdout
    for line in out.splitlines():
        if line.startswith("Detected chip:"):
            return line.split(":", 1)[1].strip()
    return None


def sh(cmd, **kw):
    return subprocess.run(cmd, check=False, capture_output=True, text=True, **kw)


@dataclasses.dataclass
class Bench:
    """Everything one run needs to know about the hardware in front of it.

    Resolved once by resolve_bench and then passed around, so the run loop
    cannot quietly re-detect a board halfway through - on this bench the board
    really does change between sessions, but never during one.
    """
    gcc: str
    probe_rs: str
    port: str
    probe: str = None            # WCH-Link serial number, None if unidentified
    select_probe: bool = False   # pass --probe through to the upload recipe
    chip: str = None             # what probe-rs reports, None if it cannot tell
    board: str = None
    pnum: str = "ANY"
    serial_index: int = None
    pins: tuple = None           # (usart, tx, rx, note)
    baud: int = 115200
    seconds: float = 4.0
    # Extra --build-property, for trying something the board definition does
    # not offer - a different build.f_cpu, say. Not a config knob so much as
    # the difference between an experiment you can re-run and one you cannot.
    properties: tuple = ()


def resolve_bench(board=None, pnum="ANY", port=None, probe=None, serial=None,
                  force=False, baud=115200, seconds=4.0, properties=(),
                  log=print) -> Bench:
    """Ask the hardware what it is, and refuse to guess when it will not say.

    Every failure here is a Failure rather than a printed line and an exit
    code, so a caller that is a test gets the reason in its own report.
    """
    gcc = find_gcc_bin()
    probe_rs_dir = find_probe_rs()
    if not gcc or not probe_rs_dir:
        missing = [n for n, v in (("toolchain", gcc), ("probe-rs", probe_rs_dir))
                   if not v]
        raise Failure(f"missing {' and '.join(missing)}; run: "
                      f"uv run tools/index/fetch_tools.py")

    port, probe_serial = resolve_port(probe, port)
    log(f"== probe {probe_serial or '(unidentified)'} -> {port}")

    chip = detected_chip(probe_rs_dir, probe_serial)
    detected = sorted(boards_for(chip)) if chip else []
    if chip:
        log(f"== target reports {chip}"
            + (f" -> {', '.join(detected)}" if detected else
               " (no boards.txt entry maps to it)"))
    else:
        log("== target chip not identified "
            "(unpowered, SWD not wired, or unknown to this probe-rs)")

    if board:
        # Boards get swapped on this bench, so an explicit board that disagrees
        # with the silicon is a mistake, not an instruction.
        if detected and board not in detected and not force:
            raise Failure(f"{chip} is attached but the board asked for is "
                          f"{board}; pass --force to flash anyway")
    elif len(detected) == 1:
        board = detected[0]
    elif detected:
        raise Failure(f"{chip} maps to several boards ({', '.join(detected)}); "
                      f"pick one")
    else:
        raise Failure("nothing to build for - name a board, or attach one "
                      "probe-rs can identify")

    if pnum == "detect":
        if not chip or chip not in boards_for(chip).get(board, []):
            raise Failure(f"a detected part number is needed that {board} "
                          f"lists; got {chip!r}")
        pnum = chip

    serial_index = serial if serial else bench_serial(board)
    if serial_index and not serial:
        log(f"== bench.json says {board} is wired to USART{serial_index}")
    pins = serial_pins(board, serial_index)
    if pins is None:
        raise Failure(f"{board}: the variant defines no default Serial port")
    n, tx, rx, note = pins
    log(f"== {board}:{pnum}  Serial = USART{n}  TX={tx}  RX={rx}"
        f"{'  (' + note + ')' if note else ''}")
    log(f"   wire {tx} -> probe RX, {rx} -> probe TX, and a common ground")

    return Bench(gcc=gcc, probe_rs=probe_rs_dir, port=port, probe=probe_serial,
                 select_probe=bool(probe_serial and probe), chip=chip,
                 board=board, pnum=pnum, serial_index=serial_index, pins=pins,
                 baud=baud, seconds=seconds, properties=tuple(properties))


def sketchbook(tmp: pathlib.Path) -> dict:
    """An arduino-cli environment whose only platform is this working tree.

    Only the sketchbook is sandboxed: it is where the platform symlink goes.
    The data directory is left alone so arduino-cli does not re-download its
    builtin tools on every run.
    """
    (tmp / "user" / "hardware" / "ch32-riscv-ug").mkdir(parents=True,
                                                        exist_ok=True)
    link = tmp / "user" / "hardware" / "ch32-riscv-ug" / "ch32v"
    if not link.exists():
        link.symlink_to(REPO)
    return dict(os.environ, ARDUINO_DIRECTORIES_USER=str(tmp / "user"))


def build(bench: Bench, sketch_dir: pathlib.Path, tmp: pathlib.Path,
          env=None, log=print) -> pathlib.Path:
    """Compile one sketch directory; return the build path or raise Failure."""
    out = tmp / "build"
    r = sh(["arduino-cli", "compile",
            "--fqbn", f"ch32-riscv-ug:ch32v:{bench.board}:pnum={bench.pnum}",
            "--build-property", f"compiler.path={bench.gcc}/",
            *(["--build-property",
               f"build.extra_flags=-DCH32_SERIAL_DEFAULT={bench.serial_index}"]
              if bench.serial_index else []),
            *[a for p in bench.properties for a in ("--build-property", p)],
            "--build-path", str(out), str(sketch_dir)],
           env=env if env is not None else sketchbook(tmp))
    if r.returncode:
        raise Failure("compile failed\n" + r.stdout + r.stderr)
    log("   " + r.stdout.strip().replace("\n", "\n   "))
    return out


def upload(bench: Bench, built: pathlib.Path, sketch_dir: pathlib.Path,
           env=None, log=print) -> None:
    """Flash it the way a user would, or raise Failure.

    The upload goes through the platform's own programmer entry, so this
    exercises programmers.txt and the probe-rs recipe rather than a side
    channel. --upload-property points the recipe at a probe-rs that may not be
    Board-Manager-installed in a symlinked dev tree.
    """
    cmd = ["arduino-cli", "upload",
           "--fqbn", f"ch32-riscv-ug:ch32v:{bench.board}:pnum={bench.pnum}",
           "--programmer", "wch-link", "--input-dir", str(built),
           "--upload-property", f"runtime.tools.probe-rs.path={bench.probe_rs}"]
    if bench.select_probe:
        cmd += ["--upload-property",
                f"upload.probe_args=--probe 1a86:8010:{bench.probe}"]
    cmd.append(str(sketch_dir))

    env = env if env is not None else sketchbook(built.parent)
    # The WCH-Link occasionally answers a flash session with "bulk read timed
    # out" and recovers on the next attempt. Retry once, but say so: a bench
    # that needs the retry every time is broken, and hiding that would make the
    # suite lie.
    r = sh(cmd, env=env)
    if r.returncode:
        log("   upload failed, retrying once:")
        log("   " + (r.stdout + r.stderr).strip().replace("\n", "\n   "))
        r = sh(cmd, env=env)
    if r.returncode:
        raise Failure("upload failed\n" + r.stdout + r.stderr)
    log("   uploaded")


def reset_target(bench: Bench) -> bool:
    """probe-rs reset, for an upload that finished with the core stopped.

    Worth trying because it is now free to try. Resetting used to eat the head
    of the output - the sketch said everything once, in setup(), and a reset
    halfway through cost exactly the lines being waited for. With a banner that
    repeats there is nothing to lose and one class of failure to recover from:
    the WCH-Link occasionally answers a flash session with "bulk read timed
    out", succeeds on the retry, and leaves the core somewhere it will not run.
    """
    if not bench.chip:
        return False
    cmd = [str(pathlib.Path(bench.probe_rs) / "probe-rs"), "reset",
           "--chip", bench.chip]
    if bench.probe:
        cmd += ["--probe", f"1a86:8010:{bench.probe}"]
    return sh(cmd).returncode == 0


def run_one(name, bench: Bench) -> dict:
    """Compile, flash and drive one sketch.

    Returns {"verdict": "pass" | "fail" | "skip", "why": ..., "output": ...}.
    A verdict with a reason rather than True/False/None: the caller may be a
    person reading a summary table or a test that has to say why it skipped,
    and None meant two unrelated things.
    """
    sketch_src = BASIC / name
    if not (sketch_src / f"{name}.ino").exists():
        return {"verdict": "fail", "why": f"no such sketch under {BASIC}",
                "output": ""}
    script = expectations(name)

    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        sketch_dir = stage_sketch(sketch_src, tmp / name)
        env = sketchbook(tmp)
        try:
            built = build(bench, sketch_dir, tmp, env)
        except Failure as e:
            print(str(e))
            return {"verdict": "fail", "why": "compile failed", "output": str(e)}

        try:
            upload(bench, built, sketch_dir, env)
        except Failure as e:
            print(str(e))
            return {"verdict": "fail", "why": "upload failed", "output": str(e)}

        import serial
        # Opened *after* the upload, not before. The WCH-Link is one composite
        # device: probe-rs drives its debug endpoint while this would be holding
        # its CDC endpoint, and this bench answers that with "bulk read timed
        # out" and then stops identifying the chip at all until the USB is
        # re-attached. Holding the port open used to be necessary to catch a
        # banner printed once; it is not, now that the banner repeats.
        with serial.Serial(bench.port, bench.baud, timeout=0.3) as uart:
            uart.reset_input_buffer()
            link = Link(uart)
            # The banner repeats every half second, so there is nothing to
            # catch in time - waiting is enough however long the flash took.
            banner = f"{name} READY"
            window = max(bench.seconds, 10.0)
            needed_reset = False
            if not link.wait(banner, window):
                # Say so rather than absorbing it: a bench that needs the reset
                # every time is broken, and hiding that would make this lie.
                needed_reset = reset_target(bench)
                print("   no banner; reset the target and waited again"
                      if needed_reset else
                      "   no banner, and the target could not be reset")
                if not (needed_reset and link.wait(banner, window)):
                    link.drain(1.0)
                    show(link.text)
                    return {"verdict": "fail", "output": link.text,
                            "why": f"no '{banner}': the sketch is not running. "
                                   f"Check that the board boots from flash - "
                                   f"probe-rs run reports the PC"}
            # Then a token, because this runner flashes sketches back to back:
            # every sketch answers PING, so a PONG the last one left in the
            # probe's FIFO would answer a bare PING. A number chosen just now
            # cannot be in output produced before it was chosen, and reading up
            # to it is what discards the rest.
            if not handshake(link):
                link.drain(1.0)
                show(link.text)
                return {"verdict": "fail", "output": link.text,
                        "why": "banner but no PONG: Serial RX is probably not "
                               "wired (the probe's TX to the board's RX)"}
            missed = replay(link, name, script, bench.seconds)

    text = link.text
    show(text)
    if missed:
        return {"verdict": "fail", "why": f"never arrived: {missed}",
                "output": text}
    # Generic rules, which also cover the steps whose text the test file
    # parametrises and this runner therefore had to skip.
    if "FAIL" in text:
        bad = [ln for ln in text.splitlines() if "FAIL" in ln]
        return {"verdict": "fail", "why": f"the sketch reported {bad}",
                "output": text}
    counts = re.findall(r"failures=(\d+)", text)
    if counts and any(c != "0" for c in counts):
        return {"verdict": "fail", "why": f"reported failures={counts}",
                "output": text}
    replayed = sum(1 for verb, _ in script if verb != "write")
    if not replayed and not counts:
        return {"verdict": "skip", "output": text,
                "why": f"nothing to check against - test_{name}.py has no "
                       f"literal expectations and the sketch reports no "
                       f"failure count"}
    return {"verdict": "pass", "output": text,
            "why": ", ".join(part for part in (
                f"{replayed} expectations" if replayed else "",
                f"failures={counts[-1]}" if counts else "",
                "needed a reset after the upload" if needed_reset else "")
                if part)}


def replay(link: Link, name: str, script: tuple, seconds: float) -> list:
    """Run the test file's script, and return the steps that never arrived.

    A sketch whose test writes nothing still gets the standard RUN, so a new
    case works here before anyone has written its expectations down.
    """
    missed = []
    if not any(verb == "write" for verb, _ in script):
        link.send("RUN\n")
    for verb, text in script:
        if verb == "write":
            link.send(text if text.endswith("\n") else text + "\n")
            continue
        ok = (link.wait(text, seconds) if verb == "expect"
              else link.match(text, seconds))
        if not ok:
            missed.append(text)
            break
    # The done line is the sketch's own verdict, so it is waited for - unless
    # the script already consumed it, in which case waiting again would sit
    # through the timeout collecting banners for a line that will not come.
    done = f"{name} done failures="
    if not any(verb != "write" and done in text for verb, text in script):
        link.wait(done, max(seconds, 30.0))
    link.drain(0.3)
    return missed


MARK = {"pass": "PASS", "fail": "FAIL", "skip": "SKIP"}


def run(sketch=DEFAULT_SKETCH, bench: Bench = None, **kw) -> dict:
    """Every sketch asked for, on one board. {name: verdict dict}."""
    if bench is None:
        bench = resolve_bench(**kw)
    results = {}
    names = sketch_names(sketch)
    for i, name in enumerate(names):
        if i:
            # A moment between cases. Back-to-back flash sessions are where
            # this bench produces "bulk read timed out", and once it does the
            # probe stops answering until the USB is re-attached. Not a fix -
            # the retry and the reset in run_one are - but it costs a second
            # and makes a sweep of eleven sketches finish more often.
            time.sleep(float(os.environ.get("CH32_SETTLE", 1.5)))
        print(f"\n===== {name}")
        results[name] = r = run_one(name, bench)
        print(f"{MARK[r['verdict']]} {name}: {r['why']}")
    return results


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--board", default=os.environ.get("CH32_BOARD"),
                    help="series board, e.g. CH32X035 "
                         "(default: whatever probe-rs detects)")
    ap.add_argument("--pnum", default=os.environ.get("CH32_PNUM", "ANY"),
                    help="part number menu entry, or 'detect' for the exact "
                         "part probe-rs reports (default: ANY, which is what "
                         "the profiles and most users build)")
    ap.add_argument("--port", default=os.environ.get("CH32_PORT"),
                    help="probe UART bridge (default: discovered)")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--probe", default=os.environ.get("CH32_PROBE"),
                    help="WCH-Link USB serial (default: the only one attached)")
    ap.add_argument("--seconds", type=float, default=4.0)
    ap.add_argument("--force", action="store_true",
                    help="flash even if the attached chip is a different series")
    ap.add_argument("--serial", type=int,
                    help="override which USART Serial is (the uart_scan test "
                         "finds it)")
    ap.add_argument("--build-property", action="append", default=[],
                    dest="properties", metavar="KEY=VALUE",
                    help="extra --build-property for the compile, repeatable")
    ap.add_argument("--sketch", default=os.environ.get("CH32_SKETCH", DEFAULT_SKETCH),
                    help=f"a directory under tests/sketches/basic, or 'all' "
                         f"(default: {DEFAULT_SKETCH})")
    args = ap.parse_args()

    try:
        bench = resolve_bench(board=args.board, pnum=args.pnum, port=args.port,
                              probe=args.probe, serial=args.serial,
                              force=args.force, baud=args.baud,
                              seconds=args.seconds, properties=args.properties)
        results = run(args.sketch, bench)
    except Failure as e:
        print(f"FAIL: {e}", file=sys.stderr)
        return 1

    if len(results) > 1:
        print(f"\n===== summary: {bench.board} ({bench.chip or 'unidentified'})")
        for name, r in results.items():
            print(f"  {MARK[r['verdict']]}  {name}")
    return 0 if all(r["verdict"] != "fail" for r in results.values()) else 1


# --- as a test ---------------------------------------------------------------
# One case per sketch; manual/conftest.py turns CH32_SKETCH into the parameter
# list and resolves `bench` once for the module. pytest is imported inside the
# test rather than at the top, because as a CLI this file runs under a bare
# `uv run`, which installs only the dependencies declared above.

def test_sketch_runs_on_the_board(bench, sketch_name):
    """Compile, flash and read back one sketch, the way a user would."""
    import pytest
    print(f"\n===== {sketch_name}")
    result = run_one(sketch_name, bench)
    print(f"{MARK[result['verdict']]} {sketch_name}: {result['why']}")
    if result["verdict"] == "skip":
        pytest.skip(result["why"])
    assert result["verdict"] == "pass", result["why"]


if __name__ == "__main__":
    sys.exit(main())
