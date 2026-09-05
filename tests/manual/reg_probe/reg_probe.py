#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.9"
# dependencies = ["pyserial>=3.5"]
# ///
"""
Purpose:
    Read the peripheral registers through the debugger while the core API is
    being exercised, and compare them with what ch32-device-data says they
    should hold. This is TEST_PLAN's method 3 ("デバッガによる状態確認"), which
    no test used until now. It sees what the self-checks cannot: the AFIO remap
    field a Serial/Wire/SPI route change writes, the CNF/MODE nibble pinMode()
    picks, which OUTDR bit a pull-up select lands on, the prescaler and reload
    a timer was given, the ADC sequence register, the interrupt-enable state in
    the PFIC. None of it needs a wire or an instrument.

    The expected values do not come from cores/arduino/ch32_registers.h. Block
    bases and register offsets come from device-data's register_map.csv, remap
    bit positions from remap_fields.csv, route pads from routes.csv, clock-
    enable bits from clock_enables.csv - so a wrong address in the core's own
    register map fails here rather than agreeing with itself.

Why manual:
    It needs the board on the bench and a probe that can read memory while the
    part runs. It is safe to leave running unattended: nothing is wired, and
    the only pads driven are PA1 (the pad every other bench sketch drives) and
    the alternate-function pads the routes under test already own. Routes that
    would land on the debug pads are skipped by name, never selected.

Required hardware:
    - One CH32 board with a WCH-Link attached (flash + Serial over one cable),
      the same wiring smoke needs
    - Nothing else

Method:
    The sketch (sketch/reg_probe.ino) is a remote control: the host tells it
    which API to call and it answers OK. After each call the host reads the
    affected registers with `probe-rs read` and checks them. The board never
    judges itself.

    Register reads while the part is running are the one thing this depends
    on, so the first check is that the sketch still answers PING after a read.

Setup:
    cd tests
    CH32_PROBE=<serial> uv run --env-file .env pytest manual/reg_probe/reg_probe.py -v -s

    or, as a CLI with a report you can read:

    uv run tests/manual/reg_probe/reg_probe.py --probe <serial>

The sketch sits in sketch/ for the same reason crt0_probe's does: a *.ino in a
test directory is how pytest-embedded recognises one of its own sketch tests,
and this one is material the driver builds and drives itself.
"""
import argparse
import csv
import dataclasses
import os
import pathlib
import re
import subprocess
import sys
import tempfile
import time

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parents[2]
sys.path.insert(0, str(REPO / "tests" / "manual" / "smoke"))
sys.path.insert(0, str(REPO / "tests" / "sketches"))

import smoke                                                    # noqa: E402
from stage import stage_sketch                                  # noqa: E402

try:
    import pytest
except ImportError:                     # the CLI does not need it
    pytest = None

BANNER_SECONDS = 20.0
CMD_SECONDS = 8.0           # per attempt; the bridge turns a line around slowly (smoke.handshake)
EXCURSION_MS = 6000         # long enough for two probe-rs reads with margin

# GPIO CNF/MODE nibble, four bits per pin. Hardware facts (reference manual,
# GPIOx_CFGLR), spelled out here rather than imported from the core so that
# the core's own header is not the yardstick it is measured with.
CFG_IN_ANALOG = 0x0
CFG_IN_FLOAT = 0x4
CFG_IN_PULL = 0x8            # OUTDR bit selects up (1) or down (0)
CFG_OUT_PP_10M = 0x1
CFG_OUT_OD_10M = 0x5
CFG_AF_PP_50M = 0xB
CFG_AF_OD_50M = 0xF

# USART CTLR1 bits, likewise from the reference manual.
USART_UE = 1 << 13
USART_TE = 1 << 3
USART_RE = 1 << 2
USART_RXNEIE = 1 << 5
USART_TXEIE = 1 << 7         # toggles while the banner goes out; masked off

PFIC_BASE = 0xE000E000      # ISR[0..7] at +0x00: readable enable state
SYSTICK_BASE = 0xE000F000

PORTS = "ABCDEF"


def debug_pads(series: str) -> set:
    """The pads the probe is talking through. Never select a route onto them.

    Single-wire SDI on the V00x line sits on PD1; the X03x line puts SWD on
    PC18/PC19; everything else is the F1-shaped PA13/PA14. An unknown series
    gets the F1 pair, which is the common case.
    """
    if series.startswith("CH32V00") or series == "CH32M007":
        return {"PD1"}
    if series.startswith("CH32X03"):
        return {"PC18", "PC19"}
    return {"PA13", "PA14"}


def _int(text: str) -> int:
    """'0x3u' / '24000000L' / '7' -> int."""
    return int(re.sub(r"[uUlL]+$", "", text.strip()), 0)


# --------------------------------------------------------------- device-data
class Tables:
    """What ch32-device-data says about this series, part and family.

    Everything the checks compare against comes from here, never from the
    core's headers: register addresses, remap bit positions, route pads,
    clock-enable bits, timer widths and the pad's default timer channel.
    """

    def __init__(self, root: pathlib.Path, series: str, part):
        self.root = pathlib.Path(root)
        self.series = series
        self.part = part
        self.family = self._family(series)
        self.reg = {}
        for row in self._rows("index/register_map.csv"):
            if row["family"] == self.family:
                self.reg[(row["block"], row["register"])] = int(row["address"], 16)
        self.remap = {}
        for row in self._rows("evidence/remap_fields.csv"):
            if row["series"] == series:
                self.remap[row["selector"]] = [
                    (p.split(":")[0], int(p.split(":")[1]))
                    for p in row["bits"].split(";")]
        self.routes = {}
        for row in self._rows("index/routes.csv"):
            if row["series"] == series:
                key = (row["selector"], int(row["value"]))
                self.routes.setdefault(key, {})[row["role"]] = row["pad"]
        self.clken = {}
        for row in self._rows("evidence/clock_enables.csv"):
            if row["family"] == self.family:
                self.clken[row["peripheral"]] = (int(row["address"], 16),
                                                 int(row["mask"], 16))
        self.timer_bits = {}
        for row in self._rows("evidence/timers.csv"):
            if row["family"] == self.family:
                self.timer_bits[row["timer"]] = int(row["counter_width_bits"])
        # Keyed by the bare pad name: pinout.csv spells some pads with their
        # system function attached (PA0-WKUP, PC13-TAMPER-RTC, PC14-OSC32_IN).
        self.pin_functions = {}
        if part:
            for row in self._rows("index/pinout.csv"):
                if row["part_number"] == part:
                    m = re.match(r"(P[A-F]\d+)", row["pad"])
                    if m:
                        self.pin_functions.setdefault(m.group(1), []).append(row)

    def _rows(self, rel: str):
        with open(self.root / rel, newline="", encoding="utf-8") as f:
            for row in csv.DictReader(f):
                yield row

    def _family(self, series: str) -> str:
        for row in self._rows("catalog/families.csv"):
            if series in row["series"].split(";"):
                return row["family"]
        raise smoke.Failure(f"{series} is in no family in catalog/families.csv")

    def addr(self, block: str, register: str) -> int:
        try:
            return self.reg[(block, register)]
        except KeyError:
            raise smoke.Failure(f"register_map.csv has no {block}.{register} "
                                f"for {self.family}")

    def has(self, block: str, register: str) -> bool:
        return (block, register) in self.reg

    def usart_block(self, n: int) -> str:
        for name in (f"USART{n}", f"UART{n}"):
            if (name, "CTLR1") in self.reg:
                return name
        raise smoke.Failure(f"register_map.csv has no USART{n}/UART{n} for "
                            f"{self.family}")

    def selector(self, peripheral: str) -> str:
        """'usart1' -> 'afio-usart1-remap' or 'afio-usart1-rm', whichever exists."""
        for name in self.remap:
            if re.fullmatch(rf"afio-{peripheral}-(remap|rm)", name):
                return name
        raise smoke.Failure(f"remap_fields.csv has no selector for {peripheral} "
                            f"on {self.series}")

    def remap_expect(self, selector: str, value: int) -> dict:
        """{register: (mask, bits)} the field holds for this route value.

        The bit list is LSB first: 'PCFR1:2;PCFR2:19;PCFR2:20' means value
        bit 0 is PCFR1 bit 2. That reading is what makes CH32L103's USART1
        route 1 land in PCFR1 bit 2 with PCFR2 untouched, which is what the
        generated variant says too - the two agreeing is one of the checks.
        """
        out = {}
        for i, (reg, bit) in enumerate(self.remap[selector]):
            mask, bits = out.get(reg, (0, 0))
            mask |= 1 << bit
            if (value >> i) & 1:
                bits |= 1 << bit
            out[reg] = (mask, bits)
        return out


# ------------------------------------------------------------------ variant
@dataclasses.dataclass
class Route:
    route: int
    pins: tuple          # pad names, or None
    value: int
    value2: int


class Variant:
    """What the generated variant header promises, read back off the text.

    Parsed rather than compiled because the checks need the numbers on the
    host, and because the point is to compare the header's promise with the
    silicon's answer and with device-data - three sources, kept apart.
    """

    def __init__(self, board: str):
        self.board = board
        text = (REPO / "variants" / board / "pins_arduino.h").read_text(encoding="utf-8")
        self.text = text
        self.pads = {m.group(1): (int(m.group(2)) << 5) | int(m.group(3))
                     for m in re.finditer(
                         r"#define (P[A-F]\d+)\s+CH32_PIN\((\d+),\s*(\d+)\)", text)}
        self.pad_of = {v: k for k, v in self.pads.items()}
        self.defs = {m.group(1): m.group(2) for m in re.finditer(
            r"^#define (\w+)\s+(\S+)\s*$", text, re.M)}
        self.routes = {}
        for m in re.finditer(r"#define CH32_(\w+)_ROUTES \{", text):
            name = m.group(1)
            body = text[m.end():text.index("\n}", m.end())]
            rows = []
            for r in re.finditer(
                    r"\{\s*(\d+),\s*\{\s*(\w+),\s*(\w+),\s*(\w+)\s*\},"
                    r"\s*(0x[0-9A-Fa-f]+)u,\s*(0x[0-9A-Fa-f]+)u\s*\}", body):
                pins = tuple(None if p == "CH32_ROUTE_NO_PIN" else p
                             for p in r.group(2, 3, 4))
                rows.append(Route(int(r.group(1)), pins, int(r.group(5), 16),
                                  int(r.group(6), 16)))
            self.routes[name] = rows
        self.pwm_timer = self._pin_map("CH32_PWM_PIN_TO_TIMER")
        self.pwm_channel = self._pin_map("CH32_PWM_PIN_TO_CHANNEL")
        self.adc_channel = self._pin_map("CH32_PIN_TO_ADC_CHANNEL")

    def _pin_map(self, macro: str) -> dict:
        m = re.search(rf"#define {macro}\(p\) \(", self.text)
        if not m:
            return {}
        # Up to the next #define: the TIMER and CHANNEL macros follow each
        # other with no blank line, and reading on into the second one gave
        # PA1 the channel number for a timer - right by coincidence on every
        # board where PA1 is TIM2_CH2, wrong on CH32V003 where it is TIM1_CH2.
        end = self.text.find("\n#define", m.end())
        body = self.text[m.end():end if end >= 0 else None]
        return {pm.group(1): int(pm.group(2))
                for pm in re.finditer(r"\(p\) == (\w+) \? (\d+) :", body)}

    def d(self, name: str, default=None):
        v = self.defs.get(name)
        if v is None:
            return default
        if re.fullmatch(r"P[A-F]\d+", v):
            return v
        try:
            return _int(v)
        except ValueError:
            return v

    def pin(self, pad) -> int:
        """Pad name -> the port-encoded number the core uses."""
        if pad is None:
            return None
        return self.pads[pad]

    def serial_numbers(self) -> list:
        return sorted(n for n in range(1, 6) if f"CH32_SERIAL{n}_TX" in self.defs)


def board_defs(board: str) -> dict:
    """build.core_defines + build.clock_defines + f_cpu + vector variant."""
    out = {}
    for line in (REPO / "boards.txt").read_text(encoding="utf-8").splitlines():
        if not line.startswith(board + ".build."):
            continue
        key, _, value = line.partition("=")
        key = key[len(board) + 7:]
        if key in ("core_defines", "clock_defines"):
            for m in re.finditer(r"-D(\w+)=(\S+)", value):
                out[m.group(1)] = _int(m.group(2))
        elif key == "f_cpu":
            out["F_CPU"] = _int(value)
        elif key == "vector_variant":
            out["vector_variant"] = value.strip()
    for need in ("F_CPU", "vector_variant", "CH32_CLOCK_USE_PLL",
                 "CH32_CLOCK_SYSCLK_HZ", "CH32_HPRE_LINEAR"):
        if need not in out:
            raise smoke.Failure(f"boards.txt: {board} has no {need}")
    return out


def irq_numbers(vector_variant: str) -> dict:
    text = (REPO / "cores" / "arduino" / f"irqn_{vector_variant}.h").read_text(encoding="utf-8")
    return {m.group(1): int(m.group(2))
            for m in re.finditer(r"#define CH32_IRQN_(\w+)\s+(\d+)", text)}


def exti_groups(vector_variant: str) -> list:
    """[(handler, mask, irq name)] from the generated exti_<v>.h."""
    text = (REPO / "cores" / "arduino" / f"exti_{vector_variant}.h").read_text(encoding="utf-8")
    return [(m.group(1), int(m.group(2), 16), m.group(3)) for m in re.finditer(
        r"X\((\w+),\s*(0x[0-9a-fA-F]+)u,\s*CH32_IRQN_(\w+)\)", text)]


def hpre_field(defs: dict) -> int:
    """The AHB prescaler field for SYSCLK/F_CPU, both encodings (ch32_clock.h)."""
    div = defs["CH32_CLOCK_SYSCLK_HZ"] // defs["F_CPU"]
    if defs["CH32_HPRE_LINEAR"]:
        table = {16: 0xB, 32: 0xC, 64: 0xD, 128: 0xE, 256: 0xF}
        return div - 1 if div <= 8 else table[div]
    table = {1: 0x0, 2: 0x8, 4: 0x9, 8: 0xA, 16: 0xB, 64: 0xC, 128: 0xD,
             256: 0xE, 512: 0xF}
    return table[div]


def tone_math(f_cpu: int, hz: int) -> tuple:
    """(psc, ticks) the way wiring_tone.cpp derives them: interrupt at 2f."""
    psc = 0
    ticks = f_cpu // (2 * hz)
    while ticks > 0x10000:
        psc += 1
        ticks = f_cpu // ((psc + 1) * 2 * hz)
    return psc, max(ticks, 1)


def spi_br(f_cpu: int, clock_hz: int) -> int:
    """First power-of-two divider that does not exceed the request (SPI.cpp)."""
    for br in range(7):
        if (f_cpu >> (br + 1)) <= clock_hz:
            return br
    return 7


def adc_divider(f_cpu: int, max_hz: int) -> int:
    div = 2
    while div < 8 and f_cpu // div > max_hz:
        div += 2
    return div


# ------------------------------------------------------------------- report
@dataclasses.dataclass
class Check:
    group: str
    name: str
    ok: object            # True / False / None (skipped)
    expected: object = None
    actual: object = None
    note: str = ""

    def describe(self) -> str:
        def fmt(v):
            return f"0x{v:X}" if isinstance(v, int) and not isinstance(v, bool) else str(v)
        mark = {True: "PASS", False: "FAIL", None: "SKIP"}[self.ok]
        s = f"{mark}  {self.group}.{self.name}"
        if self.ok is False:
            s += f"  expected={fmt(self.expected)} actual={fmt(self.actual)}"
        if self.note:
            s += f"  ({self.note})"
        return s


class Report:
    def __init__(self, log):
        self.checks = []
        self.log = log

    def _add(self, c: Check):
        self.checks.append(c)
        self.log("   " + c.describe())

    def eq(self, group, name, actual, expected, note=""):
        self._add(Check(group, name, actual == expected, expected, actual, note))

    def true(self, group, name, cond, note=""):
        self._add(Check(group, name, bool(cond), True, bool(cond), note))

    def skip(self, group, name, why):
        self._add(Check(group, name, None, note=why))

    def error(self, group, why):
        self._add(Check(group, "no_error", False, "completed", "error", why))

    def failed(self, group=None):
        return [c for c in self.checks
                if c.ok is False and (group is None or c.group == group)]

    def groups(self):
        seen = []
        for c in self.checks:
            if c.group not in seen:
                seen.append(c.group)
        return seen


# ------------------------------------------------------------------- reader
class Reader:
    """Memory reads through the WCH-Link, one process per read.

    Two implementations, because the bench is between tools: probe-rs is what
    the upload recipe still uses and is always in .tools; ch32rv is the tool
    the platform is moving to (docs/ch32rv-requests.ja.md) and reads about
    five times faster. CH32_READER=ch32rv (or --reader) picks the second.

    Either way the WCH-LinkE rewrites the target's RCC on attach (see the
    sketch header), so a read is not free: the sketch heals its clock after
    each one, and RCC itself is read through the sketch (Session.peek).
    """
    name = "?"

    def __init__(self, bench: smoke.Bench):
        self.probe = bench.probe
        self.calls = 0
        self.seconds = 0.0

    def command(self, addr: int, count: int) -> list:
        raise NotImplementedError

    def parse(self, stdout: str) -> list:
        raise NotImplementedError

    def words(self, addr: int, count: int = 1) -> list:
        cmd = self.command(addr, count)
        last = None
        for _ in range(2):
            t0 = time.time()
            proc = subprocess.run(cmd, capture_output=True, text=True)
            self.seconds += time.time() - t0
            self.calls += 1
            if proc.returncode == 0:
                out = self.parse(proc.stdout)
                if len(out) >= count:
                    return out[:count]
            last = proc.stdout + proc.stderr
        raise smoke.Failure(f"{self.name} read {hex(addr)} x{count} failed:\n{last}")

    def word(self, addr: int) -> int:
        return self.words(addr, 1)[0]


class ProbeRsReader(Reader):
    """`probe-rs read b32`. The chip name is what boards.txt hands the upload
    recipe, the same way crt0_probe does it; for a read the exact SKU does not
    matter."""
    name = "probe-rs"

    def __init__(self, bench: smoke.Bench):
        super().__init__(bench)
        self.exe = str(pathlib.Path(bench.probe_rs) / "probe-rs")
        self.chip = chip_of(bench)

    def command(self, addr, count):
        cmd = [self.exe, "read", "b32", hex(addr), str(count), "--chip", self.chip]
        if self.probe:
            cmd += ["--probe", f"1a86:8010:{self.probe}"]
        return cmd

    def parse(self, stdout):
        out = []
        for line in stdout.splitlines():
            m = re.match(r"^\s*[0-9a-fA-F]+:\s*(.*)$", line)
            if m:
                out += [int(w, 16) for w in m.group(1).split()]
        return out


class Ch32rvReader(Reader):
    """`ch32rv read --range <addr>+<bytes> --format hex-dump`, little-endian bytes."""
    name = "ch32rv"

    def __init__(self, bench: smoke.Bench, exe: str):
        super().__init__(bench)
        self.exe = exe

    def command(self, addr, count):
        cmd = [self.exe, "read", "--range", f"{addr:#x}+{4 * count}", "--format", "hex-dump",
               "-o", "-", "--non-interactive", "--progress", "none"]
        if self.probe:
            cmd += ["--probe", f"serial:{self.probe}"]
        return cmd

    def parse(self, stdout):
        data = bytearray()
        for line in stdout.splitlines():
            m = re.match(r"^\s*[0-9a-fA-F]{8}\s+((?:[0-9a-fA-F]{2}\s?){1,16})", line)
            if m:
                data += bytes.fromhex(m.group(1).replace(" ", ""))
        return [int.from_bytes(data[i:i + 4], "little") for i in range(0, len(data) - 3, 4)]


def find_ch32rv():
    """CH32_CH32RV, else ch32rv on PATH, else None."""
    import shutil
    return os.environ.get("CH32_CH32RV") or shutil.which("ch32rv")


def make_reader(bench: smoke.Bench, kind: str) -> Reader:
    kind = (kind or "probe-rs").lower()
    if kind == "ch32rv":
        exe = find_ch32rv()
        if not exe:
            raise smoke.Failure("CH32_READER=ch32rv but no ch32rv: set CH32_CH32RV=<path> "
                                "or put it on PATH")
        return Ch32rvReader(bench, exe)
    if kind in ("probe-rs", "probers"):
        return ProbeRsReader(bench)
    raise smoke.Failure(f"unknown reader {kind!r}; probe-rs or ch32rv")


def chip_of(bench: smoke.Bench) -> str:
    key = f"{bench.board}.menu.pnum.{bench.pnum}.build.probe_rs_chip="
    for line in (REPO / "boards.txt").read_text(encoding="utf-8").splitlines():
        if line.startswith(key):
            return line.split("=", 1)[1].strip()
    raise smoke.Failure(f"boards.txt has no probe_rs_chip for {bench.board}:{bench.pnum}")


# ------------------------------------------------------------------- target
class Target:
    """The sketch, driven one command at a time.

    A command is retried when it goes unanswered. Measured on CH32V003: about
    one command in eight was lost right after a probe read (the core is held
    while the probe reads, and bytes that arrive meanwhile overrun the one-byte
    receive register), and a lost tail leaves the sketch holding half a line.
    So every attempt starts with a blank line - which ends any half line and is
    not itself a command - and the count of retries is reported, because a
    bench that needs them constantly is a finding, not a nuisance to hide.
    Every command the sketch offers is idempotent except EXCURSION, which its
    caller sends with attempts=1.
    """

    def __init__(self, link: smoke.Link, log):
        self.link = link
        self.log = log
        self.retries = 0

    def cmd(self, line: str, timeout: float = CMD_SECONDS, attempts: int = 3,
            values: int = 0) -> list:
        """Send one command, wait for its OK, return any VAL lines as ints.

        `values` is how many VAL lines the answer must carry. An OK with the
        VAL missing is treated like no answer: the bytes before the OK were
        garbled, which is what the probe's clock rewrite does to a line in
        flight (seen once on CH32V203 - WIRE ROUTE answered OK, no VAL).
        """
        verb = line.split()[0]
        for attempt in range(attempts):
            before = self.link.pos
            self.link.send("\n" + line + "\n")
            if self.link.match(rf"(OK|ERR) {verb}\b[^\n]*\n", timeout):
                chunk = self.link.buf[before:self.link.pos].decode(errors="replace")
                err = re.search(rf"^ERR {verb}.*$", chunk, re.M)
                if err:
                    raise smoke.Failure(f"{line!r}: {err.group(0)}")
                vals = [int(v) for v in re.findall(r"^VAL (-?\d+)", chunk, re.M)]
                if len(vals) >= values:
                    return vals
                why = f"answered without its VAL line"
            else:
                why = f"unanswered in {timeout:g}s"
            if attempt + 1 < attempts:
                self.retries += 1
                self.log(f"   retry  {line!r} {why}")
        raise smoke.Failure(f"{line!r}: {why}, {attempts} attempts")

    def val(self, line: str, **kw) -> int:
        """cmd() for the commands that answer with one VAL."""
        return self.cmd(line, values=1, **kw)[0]

    def wait(self, pattern: str, timeout: float) -> re.Match:
        before = self.link.pos
        if not self.link.match(pattern, timeout):
            return None
        chunk = self.link.buf[before:self.link.pos].decode(errors="replace")
        return re.search(pattern, chunk)


# ------------------------------------------------------------------ session
class Session:
    """One board, one flashed sketch, one report."""

    def __init__(self, bench: smoke.Bench, target: Target, probe: Reader,
                 tables: Tables, var: Variant, defs: dict, log):
        self.bench = bench
        self.t = target
        self.p = probe
        self.dd = tables
        self.var = var
        self.defs = defs
        self.f_cpu = defs["F_CPU"]
        self.irq = irq_numbers(defs["vector_variant"])
        self.exti = exti_groups(defs["vector_variant"])
        self.rep = Report(log)
        self.log = log
        self.unsafe_pads = debug_pads(bench.board)
        mon = var.d("CH32_SERIAL_DEFAULT")
        self.monitor = mon
        self.monitor_pads = {var.d(f"CH32_SERIAL{mon}_TX"), var.d(f"CH32_SERIAL{mon}_RX")}

    # ---- register snapshots -------------------------------------------
    def peek(self, addr: int) -> int:
        """A word read by the core itself, for the registers the probe spoils."""
        return self.t.val(f"PEEK {addr:#x}")

    def sketch_rcc(self) -> dict:
        """CTLR, CFGR0 as the sketch sees them, plus how often it has healed."""
        before = self.t.link.pos
        vals = self.t.cmd("CLOCK", values=1)
        chunk = self.t.link.buf[before:self.t.link.pos].decode(errors="replace")
        m = re.search(r"^RCC ([0-9A-Fa-f]+) ([0-9A-Fa-f]+)", chunk, re.M)
        if not m:
            raise smoke.Failure("CLOCK answered without an RCC line")
        return {"CTLR": int(m.group(1), 16), "CFGR0": int(m.group(2), 16),
                "heals": vals[0] if vals else None}

    def gpio(self, pad: str) -> dict:
        pin = self.var.pin(pad)
        port, bit = pin >> 5, pin & 31
        block = "GPIO" + PORTS[port]
        base = self.dd.addr(block, "CFGLR")
        cfglr, cfghr, indr, outdr = self.p.words(base, 4)
        if bit < 8:
            nibble = (cfglr >> (bit * 4)) & 0xF
        elif bit < 16:
            nibble = (cfghr >> ((bit - 8) * 4)) & 0xF
        else:
            cfgxr = self.p.word(base + 0x1C)
            nibble = (cfgxr >> ((bit - 16) * 4)) & 0xF
        return {"cfg": nibble, "outdr": (outdr >> bit) & 1, "indr": (indr >> bit) & 1,
                "port": port, "bit": bit, "block": block}

    def rcc(self) -> dict:
        base = self.dd.addr("RCC", "CTLR")
        w = self.p.words(base, 8)
        return {base + 4 * i: v for i, v in enumerate(w)}

    def afio(self) -> dict:
        out = {"PCFR1": self.p.word(self.dd.addr("AFIO", "PCFR1"))}
        if self.dd.has("AFIO", "PCFR2"):
            out["PCFR2"] = self.p.word(self.dd.addr("AFIO", "PCFR2"))
        return out

    def exticr(self, index: int) -> int:
        return self.p.word(self.dd.addr("AFIO", "EXTICR") + 4 * index)

    def usart(self, n: int) -> dict:
        block = self.dd.usart_block(n)
        statr = self.p.word(self.dd.addr(block, "STATR"))
        # DATAR is skipped on purpose: reading it clears RXNE and would eat a
        # command byte. BRR..CTLR3 are contiguous after it.
        brr, ctlr1, ctlr2, ctlr3 = self.p.words(self.dd.addr(block, "BRR"), 4)
        return {"STATR": statr, "BRR": brr, "CTLR1": ctlr1, "CTLR2": ctlr2,
                "CTLR3": ctlr3, "block": block}

    def tim(self, name: str) -> dict:
        base = self.dd.addr(name, "CTLR1")
        names = ["CTLR1", "CTLR2", "SMCFGR", "DMAINTENR", "INTFR", "SWEVGR",
                 "CHCTLR1", "CHCTLR2", "CCER", "CNT", "PSC", "ATRLR", "RPTCR",
                 "CH1CVR", "CH2CVR", "CH3CVR", "CH4CVR", "BDTR"]
        w = self.p.words(base, len(names))
        regs = dict(zip(names, w))
        # The offsets above are the TIM layout in register_map.csv; assert it
        # rather than assume it, once per timer.
        for reg in ("CCER", "PSC", "ATRLR", "CH2CVR", "BDTR"):
            if self.dd.has(name, reg):
                assert self.dd.addr(name, reg) == base + 4 * names.index(reg), \
                    f"{name}.{reg} is not where this snapshot expects"
        return regs

    def exti_regs(self) -> dict:
        base = self.dd.addr("EXTI", "INTENR")
        names = ["INTENR", "EVENR", "RTENR", "FTENR", "SWIEVR", "INTFR"]
        return dict(zip(names, self.p.words(base, len(names))))

    def pfic_enabled(self, irqn: int) -> bool:
        isr = self.p.words(PFIC_BASE, 3)
        return bool((isr[irqn >> 5] >> (irqn & 31)) & 1)

    def i2c(self) -> dict:
        base = self.dd.addr("I2C1", "CTLR1")
        ctlr1, ctlr2, oaddr1, oaddr2 = self.p.words(base, 4)
        out = {"CTLR1": ctlr1, "CTLR2": ctlr2, "OADDR1": oaddr1, "OADDR2": oaddr2,
               "CKCFGR": self.p.word(self.dd.addr("I2C1", "CKCFGR"))}
        if self.dd.has("I2C1", "RTR"):
            out["RTR"] = self.p.word(self.dd.addr("I2C1", "RTR"))
        return out

    def spi(self) -> dict:
        ctlr1, ctlr2 = self.p.words(self.dd.addr("SPI1", "CTLR1"), 2)
        return {"CTLR1": ctlr1, "CTLR2": ctlr2}

    def adc(self) -> dict:
        base = self.dd.addr("ADC1", "STATR")
        names = ["STATR", "CTLR1", "CTLR2", "SAMPTR1", "SAMPTR2"]
        out = dict(zip(names, self.p.words(base, 5)))
        rsqr1, rsqr2, rsqr3 = self.p.words(self.dd.addr("ADC1", "RSQR1"), 3)
        out.update(RSQR1=rsqr1, RSQR2=rsqr2, RSQR3=rsqr3)
        return out

    # ---- expectation helpers ------------------------------------------
    def clock_on(self, group, name, rcc, peripheral, on=True):
        if peripheral not in self.dd.clken:
            self.rep.skip(group, name, f"clock_enables.csv has no {peripheral}")
            return
        addr, mask = self.dd.clken[peripheral]
        if addr not in rcc:
            self.rep.skip(group, name, f"{peripheral} enable at {addr:#x} outside the RCC snapshot")
            return
        self.rep.eq(group, name, bool(rcc[addr] & mask), on,
                    f"{peripheral} enable {addr:#x}&{mask:#x}")

    def pad_is(self, group, name, pad, cfg, outdr=None):
        g = self.gpio(pad)
        self.rep.eq(group, f"{name}_{pad}_cfg", g["cfg"], cfg)
        if outdr is not None:
            self.rep.eq(group, f"{name}_{pad}_outdr", g["outdr"], outdr)
        return g

    def remap_is(self, group, name, selector, value, afio=None):
        """The AFIO field for `selector` holds `value` (device-data's bits)."""
        afio = afio or self.afio()
        for reg, (mask, bits) in self.dd.remap_expect(selector, value).items():
            if reg not in afio:
                self.rep.skip(group, f"{name}_{reg}", f"{reg} not in register_map")
                continue
            self.rep.eq(group, f"{name}_{reg}", afio[reg] & mask, bits,
                        f"{selector}={value}, field mask {mask:#x}")

    def route_ok(self, route: Route) -> str:
        """None when the route may be selected, else why not.

        A route whose pads this package does not bond is left out too: the
        variant is the series union (pnum=ANY), setRoute() accepts the route,
        and the silicon ignores it - measured on CH32V203C8T6 (LQFP48), where
        USART3 route 1 (PC10/PC11) left PCFR1 at 0 and GPIOC's CFGHR at its
        reset value, from the probe and from the core alike, while PC13 on the
        same port took a pull-up as usual. Not a core defect this test can
        judge, so it is a SKIP that names the pads; the design question is in
        the README.
        """
        pads = {p for p in route.pins if p}
        hit = pads & self.unsafe_pads
        if hit:
            return f"route {route.route} lands on debug pad {sorted(hit)}"
        hit = pads & self.monitor_pads
        if hit:
            return f"route {route.route} lands on the monitor's pad {sorted(hit)}"
        if self.dd.pin_functions:
            missing = sorted(pads - set(self.dd.pin_functions))
            if missing:
                return (f"route {route.route} uses {missing}, not bonded on "
                        f"{self.dd.part}: the silicon ignores the remap")
        return None

    def variant_vs_data(self, group, name, selector, route: Route, roles):
        """The variant's pins and value for a route agree with routes.csv."""
        dd = self.dd.routes.get((selector, route.route))
        if dd is None:
            self.rep.skip(group, name, f"routes.csv has no {selector}={route.route}")
            return
        got = tuple(route.pins[i] for i in range(len(roles)))
        want = tuple(dd.get(r) for r in roles)
        self.rep.eq(group, name, got, want, f"{selector} route {route.route} {roles}")

    def link_send_excursion(self, n: int, away: Route, home: Route):
        """Send EXCURSION without waiting for its OK (see s_serial)."""
        self.t.link.send(f"\nSERIAL {n} EXCURSION {away.route} {home.route} {EXCURSION_MS}\n")

    def wait_remap(self, selector: str, value: int, seconds: float) -> bool:
        """Poll the AFIO field through the probe until it holds `value`."""
        want = self.dd.remap_expect(selector, value)
        deadline = time.time() + seconds
        while True:
            afio = self.afio()
            if all(reg in afio and (afio[reg] & mask) == bits
                   for reg, (mask, bits) in want.items()):
                return True
            if time.time() >= deadline:
                return False
            time.sleep(0.1)

    def input_pad(self) -> str:
        """A pad on another port, used as an input only, for the port math.

        Prefers a bit above 7 so CFGHR is exercised on 16-bit ports. Never a
        debug pad, never the monitor's, never PA1.
        """
        out_port = self.var.pin("PA1") >> 5
        for pad in ("PB12", "PC13", "PC0", "PB0", "PC1", "PD0"):
            if pad not in self.var.pads or pad in self.unsafe_pads or pad in self.monitor_pads:
                continue
            if (self.var.pin(pad) >> 5) == out_port:
                continue
            return pad
        return None

    # ---- scenarios -----------------------------------------------------
    def s_sanity(self):
        """Can the debugger read at all, and what does a read cost the sketch?

        The answer measured on this bench: the sketch survives, but the probe
        rewrites RCC_CFGR0 on every attach. That is recorded here as a fact
        (a PASS with the values in its note), and the sketch's heal count
        after the read shows the recovery working.
        """
        g = "sanity"
        inside = self.sketch_rcc()            # before the probe has touched anything
        self.rep.eq(g, "clock_untouched_before_first_read", inside["heals"], 0)
        rcc = self.rcc()
        self.rep.true(g, "read_while_running", bool(rcc))
        outside = rcc[self.dd.addr("RCC", "CFGR0")]
        self.rep.true(g, "probe_view_of_rcc", True,
                      f"sketch saw CFGR0={inside['CFGR0']:#010x}, probe read {outside:#010x}"
                      + ("  <- the attach rewrote RCC" if outside != inside["CFGR0"] else ""))
        self.t.link.drain(0.3)                # bytes sent at the wrong clock
        self.rep.true(g, "sketch_alive_after_read", smoke.handshake(self.t.link),
                      f"PING answered after a {self.p.name} read")
        after = self.sketch_rcc()
        self.rep.eq(g, "sketch_rcc_restored", after["CFGR0"], inside["CFGR0"],
                    f"healed {after['heals']} time(s) so far")
        # Do the enables survive the attach? clock_on() below trusts the
        # probe's view of the peripheral-clock-enable registers, so check that
        # view against the core's. The registers are found by address from
        # clock_enables.csv: their names differ per family (CH32L103 spells
        # them PB1PCENR/PB2PCENR).
        for addr in sorted({a for a, _ in self.dd.clken.values()}):
            if addr in rcc:
                self.rep.eq(g, f"probe_view_of_{addr:#x}", rcc[addr], self.peek(addr),
                            "clock enables: probe read vs the core's own read")

    def s_clock(self):
        g = "clock"
        d = self.defs
        # RCC comes from the sketch: the probe's own attach overwrites it.
        inside = self.sketch_rcc()
        ctlr, cfgr0 = inside["CTLR"], inside["CFGR0"]
        rcc = self.rcc()
        self.rep.eq(g, "hsi_on_and_ready", ctlr & 0x3, 0x3)
        use_pll = d["CH32_CLOCK_USE_PLL"]
        self.rep.eq(g, "sysclk_source", (cfgr0 >> 2) & 0x3, 2 if use_pll else 0,
                    "SWS: 0=HSI 2=PLL")
        self.rep.eq(g, "ahb_prescaler", (cfgr0 >> 4) & 0xF, hpre_field(d))
        self.rep.eq(g, "apb1_prescaler", (cfgr0 >> 8) & 0x7, 0)
        self.rep.eq(g, "apb2_prescaler", (cfgr0 >> 11) & 0x7, 0)
        if use_pll:
            self.rep.eq(g, "pll_on_and_ready", (ctlr >> 24) & 0x3, 0x3)
            self.rep.eq(g, "pll_field", cfgr0 & d["CH32_CLOCK_PLL_MASK"],
                        d["CH32_CLOCK_PLL_VALUE"])
            if d.get("CH32_CLOCK_EXTEN_ADDR"):
                v = self.peek(d["CH32_CLOCK_EXTEN_ADDR"])
                self.rep.eq(g, "exten_pll_hsi_pre", v & d["CH32_CLOCK_EXTEN_BITS"],
                            d["CH32_CLOCK_EXTEN_BITS"], "read by the core")
        mask = d.get("CH32_FLASH_ACTLR_LATENCY_MASK", 0)
        if mask:
            # Read by the core: the probe's attach raises the wait states along
            # with the clock (CH32L103 read 1 through the probe, 0 from inside).
            actlr = self.peek(self.dd.addr("FLASH", "ACTLR"))
            self.rep.eq(g, "flash_latency", actlr & mask, d["CH32_FLASH_LATENCY"],
                        "read by the core")
        else:
            self.rep.skip(g, "flash_latency", "family has no wait-state field")

        st = self.p.words(SYSTICK_BASE, 6)
        if d.get("CH32_SYSTICK_V103"):
            self.rep.eq(g, "systick_enabled", st[0] & 0x1, 0x1)
            self.rep.eq(g, "systick_compare", st[3], self.f_cpu // 8 // 1000 - 1,
                        "V103 layout: CMP at +0x0C, HCLK/8")
        else:
            self.rep.eq(g, "systick_ctlr", st[0] & 0x7, 0x7, "STE|STIE|STCLK")
            self.rep.eq(g, "systick_compare", st[4], self.f_cpu // 1000 - 1)
            if d.get("CH32_SYSTICK_64"):
                self.rep.eq(g, "systick_compare_hi", st[5], 0)
        self.rep.true(g, "pfic_systick_enabled", self.pfic_enabled(self.irq["SysTick"]))

        self.clock_on(g, "afio_clock_on", rcc, "AFIO")
        n = self.monitor
        self.clock_on(g, f"usart{n}_clock_on", rcc, self.dd.usart_block(n))
        for pad in self.monitor_pads:
            self.clock_on(g, f"gpio{pad[1]}_clock_on", rcc, "GPIO" + pad[1])

    def s_gpio(self):
        g = "gpio"
        out = "PA1"
        pin = self.var.pin(out)
        self.t.cmd(f"PINMODE {pin} OUT")
        self.pad_is(g, "output", out, CFG_OUT_PP_10M)
        self.clock_on(g, "gpioa_clock_on", self.rcc(), "GPIOA")
        self.t.cmd(f"DWRITE {pin} 1")
        gp = self.gpio(out)
        self.rep.eq(g, "write_high_outdr", gp["outdr"], 1)
        self.rep.eq(g, "write_high_indr", gp["indr"], 1)
        self.rep.eq(g, "write_high_digitalRead", self.t.val(f"DREAD {pin}"), 1)
        self.t.cmd(f"DWRITE {pin} 0")
        gp = self.gpio(out)
        self.rep.eq(g, "write_low_outdr", gp["outdr"], 0)
        self.rep.eq(g, "write_low_indr", gp["indr"], 0)
        self.t.cmd(f"PINMODE {pin} OD")
        self.pad_is(g, "open_drain", out, CFG_OUT_OD_10M)
        self.t.cmd(f"PINMODE {pin} IN")
        self.pad_is(g, "input", out, CFG_IN_FLOAT)

        pad = self.input_pad()
        if not pad:
            self.rep.skip(g, "second_port", "no free pad on another port")
            return
        pin2 = self.var.pin(pad)
        self.t.cmd(f"PINMODE {pin2} PU")
        gp = self.pad_is(g, "pullup", pad, CFG_IN_PULL, outdr=1)
        self.rep.eq(g, f"pullup_{pad}_reads", self.t.val(f"DREAD {pin2}"), gp["indr"],
                    "digitalRead agrees with INDR")
        self.log(f"   info  {pad} with pull-up reads INDR={gp['indr']} "
                 f"(1 unless something external holds it)")
        self.t.cmd(f"PINMODE {pin2} PD")
        gp = self.pad_is(g, "pulldown", pad, CFG_IN_PULL, outdr=0)
        self.log(f"   info  {pad} with pull-down reads INDR={gp['indr']}")
        self.t.cmd(f"PINMODE {pin2} IN")
        self.pad_is(g, "input", pad, CFG_IN_FLOAT)
        self.clock_on(g, f"gpio{pad[1]}_clock_on", self.rcc(), "GPIO" + pad[1])

    def usart_state(self, g, n, route: Route, baud=115200, tag=""):
        sel = self.dd.selector(f"usart{n}")
        tag = tag or f"route{route.route}"
        afio = self.afio()
        self.remap_is(g, f"{tag}_remap", sel, route.route, afio)
        self.rep.eq(g, f"{tag}_variant_value_vs_data",
                    (route.value, route.value2),
                    tuple(bits for reg in ("PCFR1", "PCFR2")
                          for (m, bits) in [self.dd.remap_expect(sel, route.route).get(reg, (0, 0))]),
                    "variant table value/value2 against remap_fields.csv")
        self.variant_vs_data(g, f"{tag}_variant_pins_vs_data", sel, route, ("TX", "RX"))
        tx, rx = route.pins[0], route.pins[1]
        self.pad_is(g, f"{tag}_tx", tx, CFG_AF_PP_50M)
        self.pad_is(g, f"{tag}_rx", rx, CFG_IN_PULL, outdr=1)
        u = self.usart(n)
        want = USART_UE | USART_TE | USART_RE | USART_RXNEIE
        self.rep.eq(g, f"{tag}_ctlr1", u["CTLR1"] & ~USART_TXEIE & 0xFFFF, want,
                    "UE|TE|RE|RXNEIE, TXEIE masked")
        self.rep.eq(g, f"{tag}_brr", u["BRR"] & 0xFFFF, (self.f_cpu + baud // 2) // baud)
        self.rep.eq(g, f"{tag}_ctlr2", u["CTLR2"] & 0xFFFF, 0)
        self.rep.eq(g, f"{tag}_ctlr3", u["CTLR3"] & 0xFFFF, 0)
        self.clock_on(g, f"{tag}_clock_on", self.rcc(), u["block"])
        irq = self.var.d(f"CH32_SERIAL{n}_IRQ")
        irqn = self.irq.get(str(irq).replace("CH32_IRQN_", ""))
        if irqn is None:
            self.rep.skip(g, f"{tag}_pfic", f"unknown irq {irq}")
        else:
            self.rep.true(g, f"{tag}_pfic_enabled", self.pfic_enabled(irqn), f"irq {irqn}")

    def s_serial(self):
        for n in self.var.serial_numbers():
            g = "serial"
            routes = self.var.routes.get(f"SERIAL{n}", [])
            if not routes:
                self.rep.skip(g, f"usart{n}", "variant has no route table")
                continue
            if n == self.monitor:
                self.usart_state(g, n, routes[0], tag=f"usart{n}_monitor")
                continue
            self.t.cmd(f"SERIAL {n} BEGIN")
            self.usart_state(g, n, routes[0], tag=f"usart{n}_route{routes[0].route}")
            prev = routes[0]
            for r in routes[1:]:
                why = self.route_ok(r)
                tag = f"usart{n}_route{r.route}"
                if why:
                    self.rep.skip(g, tag, why)
                    continue
                self.rep.eq(g, f"{tag}_accepted", self.t.val(f"SERIAL {n} ROUTE {r.route}"), 1)
                self.usart_state(g, n, r, tag=tag)
                for old in prev.pins[:2]:
                    if old and old not in r.pins:
                        self.pad_is(g, f"{tag}_released", old, CFG_IN_FLOAT)
                prev = r
            if prev is not routes[0]:
                self.rep.eq(g, f"usart{n}_back_accepted",
                            self.t.val(f"SERIAL {n} ROUTE {routes[0].route}"), 1)
                self.usart_state(g, n, routes[0], tag=f"usart{n}_back")
            self.t.cmd(f"SERIAL {n} END")
            u = self.usart(n)
            self.rep.eq(g, f"usart{n}_end_ctlr1", u["CTLR1"] & 0xFFFF, 0)

        # The monitor itself: move it, look while it is away, and let it return.
        g = "serial"
        n = self.monitor
        routes = self.var.routes.get(f"SERIAL{n}", [])
        away = next((r for r in routes[1:] if not self.route_ok(r)), None)
        if not away:
            reasons = [self.route_ok(r) for r in routes[1:]]
            self.rep.skip(g, f"usart{n}_excursion",
                          "; ".join(reasons) if reasons else "one route only")
            return
        home = routes[0]
        sel = self.dd.selector(f"usart{n}")
        # The OK cannot be the cue: the probe's UART bridge can hold a line for
        # seconds (the CH549 WCH-Link on the CH32V103 held it past the whole
        # six-second excursion, so the reads saw the port back home and every
        # check failed). The registers themselves say when the port has left,
        # so poll the remap field until it shows the away route.
        self.link_send_excursion(n, away, home)
        t0 = time.time()
        left = self.wait_remap(sel, away.route, EXCURSION_MS / 1000)
        self.rep.true(g, f"usart{n}_excursion_left", left,
                      f"remap field showed route {away.route} after {time.time() - t0:.1f}s")
        try:
            self.remap_is(g, f"usart{n}_excursion_remap", sel, away.route)
            self.pad_is(g, f"usart{n}_excursion_tx", away.pins[0], CFG_AF_PP_50M)
            self.pad_is(g, f"usart{n}_excursion_rx", away.pins[1], CFG_IN_PULL, outdr=1)
            for old in home.pins[:2]:
                if old not in away.pins:
                    self.pad_is(g, f"usart{n}_excursion_released", old, CFG_IN_FLOAT)
        except smoke.Failure as e:
            self.rep.error(g, f"reading during the excursion: {e}")
        took = time.time() - t0
        self.rep.true(g, f"usart{n}_excursion_reads_fit_window", took < EXCURSION_MS / 1000,
                      f"{took:.1f}s of {EXCURSION_MS / 1000:g}s")
        m = self.t.wait(r"DONE (\d) (\d)", EXCURSION_MS / 1000 + 15)
        self.rep.true(g, f"usart{n}_excursion_returned", m is not None and m.groups() == ("1", "1"),
                      m.group(0) if m else "no DONE line")
        self.usart_state(g, n, home, tag=f"usart{n}_home")
        for old in away.pins[:2]:
            if old not in home.pins:
                self.pad_is(g, f"usart{n}_home_released", old, CFG_IN_FLOAT)
        self.rep.true(g, f"usart{n}_alive_after_excursion", smoke.handshake(self.t.link))

    def s_exti(self):
        g = "exti"
        pads = ["PA1"]
        second = self.input_pad()
        if second:
            pads.append(second)
        for pad in pads:
            pin = self.var.pin(pad)
            port, bit = pin >> 5, pin & 31
            self.t.cmd(f"PINMODE {pin} IN")
            group = next(((h, m, i) for (h, m, i) in self.exti if m & (1 << bit)), None)
            for mode, (rt, ft) in (("RISING", (1, 0)), ("FALLING", (0, 1)),
                                   ("CHANGE", (1, 1)), ("LOW", (0, 1))):
                self.t.cmd(f"EXTI {pin} {mode}")
                cr = self.exticr(bit >> 2)
                self.rep.eq(g, f"{pad}_{mode}_exticr_port", (cr >> ((bit & 3) * 4)) & 0xF, port,
                            f"EXTICR{bit >> 2} nibble {bit & 3}")
                ex = self.exti_regs()
                self.rep.eq(g, f"{pad}_{mode}_rising", (ex["RTENR"] >> bit) & 1, rt)
                self.rep.eq(g, f"{pad}_{mode}_falling", (ex["FTENR"] >> bit) & 1, ft)
                self.rep.eq(g, f"{pad}_{mode}_enabled", (ex["INTENR"] >> bit) & 1, 1)
                if group:
                    self.rep.true(g, f"{pad}_{mode}_pfic", self.pfic_enabled(self.irq[group[2]]),
                                  f"{group[0]} irq {self.irq[group[2]]}")
                else:
                    self.rep.skip(g, f"{pad}_{mode}_pfic", f"no EXTI vector covers line {bit}")
            self.t.cmd(f"EXTI {pin} DETACH")
            ex = self.exti_regs()
            self.rep.eq(g, f"{pad}_detach_enabled", (ex["INTENR"] >> bit) & 1, 0)
            self.rep.eq(g, f"{pad}_detach_rising", (ex["RTENR"] >> bit) & 1, 0)
            self.rep.eq(g, f"{pad}_detach_falling", (ex["FTENR"] >> bit) & 1, 0)

    def s_pwm(self):
        g = "pwm"
        pad = "PA1"
        pin = self.var.pin(pad)
        timer = self.var.pwm_timer.get(pad)
        ch = self.var.pwm_channel.get(pad)
        if not timer:
            self.rep.skip(g, "pa1", "variant gives PA1 no PWM timer")
            return
        name = f"TIM{timer}"
        dd = {(r["peripheral"], r["role"]) for r in self.dd.pin_functions.get(pad, [])
              if r["route"] == "default" and r["peripheral"].startswith("TIM")
              and re.fullmatch(r"CH\d", r["role"])}
        if dd:
            self.rep.true(g, "variant_channel_vs_pinout", (name, f"CH{ch}") in dd,
                          f"variant {name}_CH{ch}; pinout default {sorted(dd)}")
        else:
            self.rep.skip(g, "variant_channel_vs_pinout", "no pinout rows for this part")

        steps = 256
        prescale = max(1, self.f_cpu // (1000 * steps))
        for value, duty in ((64, 64), (128, 128), (255, 256), (0, 0)):
            self.t.cmd(f"AWRITE {pin} {value}")
            tm = self.tim(name)
            self.rep.eq(g, f"v{value}_ch{ch}cvr", tm[f"CH{ch}CVR"] & 0xFFFF, duty)
            if value == 64:
                self.rep.eq(g, "psc", tm["PSC"] & 0xFFFF, prescale - 1)
                self.rep.eq(g, "atrlr", tm["ATRLR"] & 0xFFFF, steps - 1)
                self.rep.eq(g, "ctlr1_arpe_cen", tm["CTLR1"] & 0x81, 0x81)
                word = tm["CHCTLR1"] if ch <= 2 else tm["CHCTLR2"]
                shift = ((ch - 1) & 1) * 8
                self.rep.eq(g, f"ch{ch}_ocmode_pwm1", (word >> shift) & 0xFF, 0x68)
                self.rep.eq(g, f"ch{ch}_ccer_enable", (tm["CCER"] >> ((ch - 1) * 4)) & 1, 1)
                if timer == 1:
                    self.rep.eq(g, "tim1_bdtr_moe", (tm["BDTR"] >> 15) & 1, 1)
                self.pad_is(g, "pwm", pad, CFG_AF_PP_50M)
                self.clock_on(g, f"{name.lower()}_clock_on", self.rcc(), name)
        self.t.cmd(f"PINMODE {pin} IN")

        dac1 = self.var.d("CH32_DAC1_PIN")
        if dac1:
            g = "dac"
            self.t.cmd(f"AWRITE {self.var.pin(dac1)} 128")
            ctlr = self.p.word(self.dd.addr("DAC", "CTLR"))
            dhr = self.p.word(self.dd.addr("DAC", "R12BDHR1"))
            self.rep.eq(g, "en1", ctlr & 1, 1)
            self.rep.eq(g, "r12bdhr1", dhr & 0xFFF, (128 * 0xFFF) // 255)
            self.pad_is(g, "analog", dac1, CFG_IN_ANALOG)
            self.clock_on(g, "dac_clock_on", self.rcc(), "DAC")
            self.t.cmd(f"AWRITE {self.var.pin(dac1)} 0")
            self.rep.eq(g, "r12bdhr1_zero", self.p.word(self.dd.addr("DAC", "R12BDHR1")) & 0xFFF, 0)

    def s_adc(self):
        g = "adc"
        a0 = self.var.d("A0")
        if not a0:
            self.rep.skip(g, "a0", "variant has no A0")
            return
        pin = self.var.pin(a0)
        ch = self.var.adc_channel.get(a0)
        dd = [int(r["role"][2:]) for r in self.dd.pin_functions.get(a0, [])
              if r["peripheral"] == "ADC1" and re.fullmatch(r"IN\d+", r["role"])]
        if dd:
            self.rep.eq(g, "variant_channel_vs_pinout", ch, dd[0], f"{a0}")
        else:
            self.rep.skip(g, "variant_channel_vs_pinout", "no pinout rows for this part")
        v = self.t.val(f"AREAD {pin}")
        self.rep.true(g, "value_in_range", 0 <= v <= 1023, f"analogRead({a0}) = {v}")
        div = adc_divider(self.f_cpu, self.defs["CH32_ADC_MAX_HZ"])
        cfgr0 = self.peek(self.dd.addr("RCC", "CFGR0"))
        self.rep.eq(g, "adcpre", (cfgr0 >> 14) & 0x3, div // 2 - 1, f"HCLK/{div}, read by the core")
        a = self.adc()
        adon, exttrig, swstart_sel = 1 << 0, 1 << 20, 7 << 17
        self.rep.eq(g, "ctlr2_adon_exttrig_swstart", a["CTLR2"] & (adon | exttrig | swstart_sel),
                    adon | exttrig | swstart_sel)
        self.rep.eq(g, "rsqr3_channel", a["RSQR3"] & 0x1F, ch)
        self.rep.eq(g, "rsqr1_length_one", (a["RSQR1"] >> 20) & 0xF, 0)
        self.rep.eq(g, "samptr1_longest", a["SAMPTR1"] & 0x00FFFFFF, 0x00FFFFFF)
        self.rep.eq(g, "samptr2_longest", a["SAMPTR2"] & 0x3FFFFFFF, 0x3FFFFFFF)
        rcc = self.rcc()
        self.rep.true(g, "adcclk_within_limit", self.f_cpu // div <= self.defs["CH32_ADC_MAX_HZ"],
                      f"{self.f_cpu // div} Hz <= {self.defs['CH32_ADC_MAX_HZ']}")
        self.pad_is(g, "analog", a0, CFG_IN_ANALOG)
        self.clock_on(g, "adc1_clock_on", rcc, "ADC1")

    def s_tone(self):
        g = "tone"
        timer = self.var.d("CH32_TONE_TIMER")
        if not timer:
            self.rep.skip(g, "timer", "variant has no tone timer")
            return
        name = f"TIM{timer}"
        bits = self.var.d("CH32_TONE_TIMER_BITS", 16)
        dd_bits = self.dd.timer_bits.get(name)
        if dd_bits:
            self.rep.eq(g, "variant_bits_vs_timers_csv", bits, dd_bits, name)
        pad = "PA1"
        pin = self.var.pin(pad)
        hz = 500
        psc, ticks = tone_math(self.f_cpu, hz)
        self.t.cmd(f"TONE {pin} {hz}")
        tm = self.tim(name)
        self.rep.eq(g, "psc", tm["PSC"] & 0xFFFF, psc)
        if bits == 32:
            self.rep.eq(g, "atrlr_32bit", tm["ATRLR"], ticks - 1,
                        "a 16-bit store would leave the value in both halves")
        else:
            self.rep.eq(g, "atrlr", tm["ATRLR"] & 0xFFFF, ticks - 1)
        self.rep.eq(g, "ctlr1_cen", tm["CTLR1"] & 0x1, 0x1)
        self.rep.eq(g, "dmaintenr_uie", tm["DMAINTENR"] & 0x1, 0x1)
        irq = str(self.var.d("CH32_TONE_TIMER_IRQ")).replace("CH32_IRQN_", "")
        if irq in self.irq:
            self.rep.true(g, "pfic_enabled", self.pfic_enabled(self.irq[irq]), f"{irq} = {self.irq[irq]}")
        self.pad_is(g, "pin_output", pad, CFG_OUT_PP_10M)
        self.clock_on(g, f"{name.lower()}_clock_on", self.rcc(), name)
        self.t.cmd(f"NOTONE {pin}")
        tm = self.tim(name)
        self.rep.eq(g, "notone_ctlr1", tm["CTLR1"] & 0xFFFF, 0)
        self.rep.eq(g, "notone_dmaintenr", tm["DMAINTENR"] & 0xFFFF, 0)
        if irq in self.irq:
            self.rep.true(g, "notone_pfic_disabled", not self.pfic_enabled(self.irq[irq]))
        self.t.cmd(f"PINMODE {pin} IN")

    def i2c_state(self, g, route: Route, tag: str, clock_hz: int):
        sel = self.dd.selector("i2c1")
        self.remap_is(g, f"{tag}_remap", sel, route.route)
        self.variant_vs_data(g, f"{tag}_variant_pins_vs_data", sel, route, ("SCL", "SDA"))
        self.pad_is(g, f"{tag}_scl", route.pins[0], CFG_AF_OD_50M)
        self.pad_is(g, f"{tag}_sda", route.pins[1], CFG_AF_OD_50M)
        i = self.i2c()
        mhz = self.f_cpu // 1_000_000
        self.rep.eq(g, f"{tag}_pe", i["CTLR1"] & 1, 1)
        self.rep.eq(g, f"{tag}_freq_field", i["CTLR2"] & 0x3F, mhz & 0x3F, f"PCLK1 {mhz} MHz")
        self.rep.true(g, f"{tag}_freq_fits_field", mhz <= 0x3F,
                      f"FREQ[5:0] cannot hold {mhz} MHz" if mhz > 0x3F else f"{mhz} MHz")
        if clock_hz <= 100_000:
            self.rep.eq(g, f"{tag}_ckcfgr", i["CKCFGR"] & 0x8FFF, max(4, self.f_cpu // (2 * clock_hz)),
                        "standard mode: FS=0, CCR=PCLK1/(2f)")
            rtr = mhz + 1
        else:
            self.rep.eq(g, f"{tag}_ckcfgr", i["CKCFGR"] & 0x8FFF,
                        0x8000 | max(1, self.f_cpu // (3 * clock_hz)), "fast mode: FS=1, CCR=PCLK1/(3f)")
            rtr = (mhz * 300) // 1000 + 1
        if "RTR" in i:
            self.rep.eq(g, f"{tag}_rtr", i["RTR"] & 0x3F, rtr & 0x3F)
            self.rep.true(g, f"{tag}_rtr_fits_field", rtr <= 0x3F,
                          f"TRISE[5:0] cannot hold {rtr}" if rtr > 0x3F else str(rtr))
        self.clock_on(g, f"{tag}_clock_on", self.rcc(), "I2C1")

    def s_wire(self):
        g = "wire"
        routes = self.var.routes.get("I2C1", [])
        if not routes:
            self.rep.skip(g, "i2c1", "variant has no I2C1 route table")
            return
        self.t.cmd("WIRE BEGIN")
        self.i2c_state(g, routes[0], f"route{routes[0].route}", 100_000)
        self.t.cmd("WIRE CLOCK 400000")
        self.i2c_state(g, routes[0], f"route{routes[0].route}_400k", 400_000)
        self.t.cmd("WIRE CLOCK 100000")
        prev = routes[0]
        for r in routes[1:]:
            why = self.route_ok(r)
            tag = f"route{r.route}"
            if why:
                self.rep.skip(g, tag, why)
                continue
            self.rep.eq(g, f"{tag}_accepted", self.t.val(f"WIRE ROUTE {r.route}"), 1)
            self.i2c_state(g, r, tag, 100_000)
            for old in prev.pins[:2]:
                if old and old not in r.pins:
                    self.pad_is(g, f"{tag}_released", old, CFG_IN_FLOAT)
            prev = r
        if prev is not routes[0]:
            self.rep.eq(g, "back_accepted", self.t.val(f"WIRE ROUTE {routes[0].route}"), 1)
            self.i2c_state(g, routes[0], "back", 100_000)
        self.t.cmd("WIRE END")
        # end() clears CTLR1 and then stops the peripheral's clock, after which
        # its registers read back as bus noise (1, 0x9, 0x21 were seen), so the
        # clock bit is the thing to check.
        self.clock_on(g, "end_clock_off", self.rcc(), "I2C1", on=False)
        self.log(f"   info  I2C1 CTLR1 reads {self.i2c()['CTLR1']:#x} with its clock off (not meaningful)")

    def spi_state(self, g, route: Route, tag: str, clock_hz=4_000_000, mode=0, lsb=False):
        sel = self.dd.selector("spi1")
        self.remap_is(g, f"{tag}_remap", sel, route.route)
        self.variant_vs_data(g, f"{tag}_variant_pins_vs_data", sel, route, ("SCK", "MISO", "MOSI"))
        self.pad_is(g, f"{tag}_sck", route.pins[0], CFG_AF_PP_50M)
        self.pad_is(g, f"{tag}_miso", route.pins[1], CFG_IN_PULL, outdr=1)
        self.pad_is(g, f"{tag}_mosi", route.pins[2], CFG_AF_PP_50M)
        s = self.spi()
        mstr, ssm, ssi, spe = 1 << 2, 1 << 9, 1 << 8, 1 << 6
        want = mstr | ssm | ssi | spe | (spi_br(self.f_cpu, clock_hz) << 3)
        want |= (mode & 1) | ((mode >> 1) & 1) << 1 | (1 << 7 if lsb else 0)
        self.rep.eq(g, f"{tag}_ctlr1", s["CTLR1"] & 0xFFFF, want,
                    f"MSTR|SSM|SSI|SPE|BR={spi_br(self.f_cpu, clock_hz)} mode{mode}"
                    f"{' LSBFIRST' if lsb else ''}")
        self.clock_on(g, f"{tag}_clock_on", self.rcc(), "SPI1")

    def s_spi(self):
        g = "spi"
        routes = self.var.routes.get("SPI1", [])
        if not routes:
            self.rep.skip(g, "spi1", "variant has no SPI1 route table")
            return
        self.t.cmd("SPI BEGIN")
        self.spi_state(g, routes[0], f"route{routes[0].route}")
        self.t.cmd("SPI SETTINGS 1000000 3 1")
        self.spi_state(g, routes[0], f"route{routes[0].route}_1MHz_mode3_lsb", 1_000_000, 3, True)
        self.t.cmd("SPI SETTINGS 4000000 0 0")
        prev = routes[0]
        for r in routes[1:]:
            why = self.route_ok(r)
            tag = f"route{r.route}"
            if why:
                self.rep.skip(g, tag, why)
                continue
            self.rep.eq(g, f"{tag}_accepted", self.t.val(f"SPI ROUTE {r.route}"), 1)
            self.spi_state(g, r, tag)
            for old in prev.pins:
                if old and old not in r.pins:
                    self.pad_is(g, f"{tag}_released", old, CFG_IN_FLOAT)
            prev = r
        if prev is not routes[0]:
            self.rep.eq(g, "back_accepted", self.t.val(f"SPI ROUTE {routes[0].route}"), 1)
            self.spi_state(g, routes[0], "back")
        self.t.cmd("SPI END")
        self.rep.eq(g, "end_ctlr1", self.spi()["CTLR1"] & 0xFFFF, 0)
        self.clock_on(g, "end_clock_off", self.rcc(), "SPI1", on=False)

    SCENARIOS = ("s_sanity", "s_clock", "s_gpio", "s_serial", "s_exti", "s_pwm",
                 "s_adc", "s_tone", "s_wire", "s_spi")

    def run_all(self, only=None):
        for name in self.SCENARIOS:
            if only and name[2:] not in only:
                continue
            self.log(f"-- {name[2:]}")
            try:
                getattr(self, name)()
            except smoke.Failure as e:
                self.rep.error(name[2:], str(e))
            except AssertionError as e:
                self.rep.error(name[2:], f"snapshot layout: {e}")
            except Exception as e:                    # noqa: BLE001 - one scenario must not end the run
                self.rep.error(name[2:], f"{type(e).__name__}: {e}")


# --------------------------------------------------------------------- run
def run(bench: smoke.Bench, log=print, only=None, reader=None) -> dict:
    reader = reader or os.environ.get("CH32_READER", "probe-rs")
    tables_root = smoke.find_tables()
    if not tables_root:
        raise smoke.Failure("ch32-device-data not found; run: uv run tools/index/fetch_tools.py")
    var = Variant(bench.board)
    defs = board_defs(bench.board)
    part = bench.chip if bench.chip and bench.chip in smoke.boards_for(bench.chip).get(bench.board, []) else None
    tables = Tables(pathlib.Path(tables_root), bench.board, part)
    if not part:
        log("   note: exact part number unknown, pinout cross-checks are skipped")

    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        sketch_dir = stage_sketch(HERE / "sketch", tmp / "reg_probe")
        env = smoke.sketchbook(tmp)
        built = smoke.build(bench, sketch_dir, tmp, env, log=log)
        import serial
        with serial.Serial(bench.port, bench.baud, timeout=0.3) as uart:
            uart.reset_input_buffer()
            link = smoke.Link(uart)
            smoke.upload(bench, built, sketch_dir, env, log=log)
            if not link.wait("reg_probe READY", BANNER_SECONDS):
                log("   no banner after upload; resetting")
                smoke.reset_target(bench)
                if not link.wait("reg_probe READY", BANNER_SECONDS):
                    raise smoke.Failure(
                        f"no 'reg_probe READY' within {BANNER_SECONDS:g}s: the sketch "
                        f"is not running, or Serial is miswired")
            if not smoke.handshake(link):
                raise smoke.Failure("banner arrives but PING is not answered: the probe's "
                                    "TX is probably not wired to the board's RX")
            probe = make_reader(bench, reader)
            log(f"== reading registers with {probe.name} ({probe.exe})")
            target = Target(link, log)
            session = Session(bench, target, probe, tables, var, defs, log)
            t0 = time.time()
            session.run_all(only)
            elapsed = time.time() - t0
            heals = None
            try:
                heals = session.sketch_rcc()["heals"]
            except smoke.Failure:
                pass
            log(f"-- {probe.calls} {probe.name} reads in {probe.seconds:.1f}s; "
                f"{elapsed:.0f}s for the whole session; the sketch healed its clock "
                f"{heals} time(s); {target.retries} command(s) had to be repeated")
            return {"report": session.rep, "output": link.text, "board": bench.board,
                    "chip": bench.chip, "reader": probe.name, "reads": probe.calls,
                    "read_seconds": probe.seconds, "heals": heals, "seconds": elapsed,
                    "retries": target.retries}


def summary(result: dict) -> str:
    rep = result["report"]
    lines = [f"===== reg_probe: {result['board']} ({result['chip']})  "
             f"{result['reads']} reads via {result['reader']} in {result['read_seconds']:.1f}s, "
             f"{result['seconds']:.0f}s total, {result['heals']} clock heals, "
             f"{result['retries']} command retries"]
    for g in rep.groups():
        checks = [c for c in rep.checks if c.group == g]
        n_pass = sum(1 for c in checks if c.ok is True)
        n_fail = sum(1 for c in checks if c.ok is False)
        n_skip = sum(1 for c in checks if c.ok is None)
        mark = "FAIL" if n_fail else "PASS"
        lines.append(f"  {mark}  {g:8s} {n_pass:3d} pass {n_fail:3d} fail {n_skip:3d} skip")
    for c in rep.failed():
        lines.append("  " + c.describe())
    return "\n".join(lines)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--board")
    ap.add_argument("--pnum", default="ANY")
    ap.add_argument("--port")
    ap.add_argument("--probe", help="WCH-Link USB serial, when several are attached")
    ap.add_argument("--force", action="store_true")
    ap.add_argument("--only", help="comma-separated scenario names, e.g. gpio,serial")
    ap.add_argument("--reader", choices=["probe-rs", "ch32rv"],
                    default=os.environ.get("CH32_READER", "probe-rs"),
                    help="which tool reads the registers (CH32_READER); ch32rv needs "
                         "CH32_CH32RV=<path> or ch32rv on PATH")
    args = ap.parse_args()
    try:
        bench = smoke.resolve_bench(board=args.board, pnum=args.pnum, port=args.port,
                                    probe=args.probe, force=args.force)
        result = run(bench, only=args.only.split(",") if args.only else None,
                     reader=args.reader)
    except smoke.Failure as e:
        print(str(e), file=sys.stderr)
        return 2
    print(summary(result))
    return 1 if result["report"].failed() else 0


# ------------------------------------------------------------------ pytest
if pytest:
    @pytest.fixture(scope="module")
    def registers(bench):
        try:
            return run(bench)
        except smoke.Failure as e:
            pytest.fail(str(e))

    @pytest.mark.parametrize("group", ["sanity", "clock", "gpio", "serial", "exti",
                                       "pwm", "dac", "adc", "tone", "wire", "spi"])
    def test_registers(registers, group):
        """Every register the group looked at held what device-data says.

        Expected result (pass):  every check in the group is PASS or SKIP.
        Expected result (fail):  a named register, its expected and actual
                                 value - the check name says which API call
                                 preceded the read.
        """
        checks = [c for c in registers["report"].checks if c.group == group]
        if not checks:
            pytest.skip(f"{group}: nothing to check on {registers['board']}")
        failed = [c for c in checks if c.ok is False]
        assert not failed, "\n".join(c.describe() for c in failed)


if __name__ == "__main__":
    sys.exit(main())
