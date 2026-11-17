"""uart-dma: Serial with setRxBufferSize() / setTxBufferSize(), the buffers moved by DMA (HardwareSerialDmaRx.cpp,
HardwareSerialDmaTx.cpp), on the probe's UART and the DUT's TX / RX lines (OEP ports with <port>.rx@<pad> and
<port>.tx@<pad> labelled, as uart.formats). The DUT runs sketch_support/uart_dma/uart_dma.ino.

Every test first begins Serial with a 512-byte receive and a 256-byte transmit buffer (CH32V003's 2 KB of RAM holds
them) and checks that the USART's DMA requests (CTLR3 DMAR / DMAT) are on: a port that fell back to the built-in path
fails here, not later as lost bytes.

uart.dma-receive (test_uart_dma_receive): 4096 bytes from the probe at 1 Mbaud and 2 Mbaud (where the DUT's USART
reaches it: dmax, its clock / 16), the DUT reading all the while: every byte, in order (count and FNV-1a), no error.
uart.dma-interrupts-off (test_uart_dma_interrupts_off): at 1 Mbaud, 400 bytes while the DUT holds interrupts off for
1 s - all 400 already in the buffer when they come back (the DMA's count, read with interrupts still off), then read
whole; and 640 (a quarter more than the buffer): UART_BUFFER_FULL_ERROR and the newest 511 kept.
uart.dma-transmit (test_uart_dma_transmit): 2048 bytes written at 1 Mbaud and 2 Mbaud, eight times the buffer: the
probe's UART and WireSkein get every byte, and the DUT's time from the first write to flush() is the longer of the
line's and 2048 write()s' (dwcost) within 5 % and 1 ms; then the same written with interrupts off.
uart.dma-receive-event (test_uart_dma_receive_event): onReceive() on a DMA buffer at 115200, as uart.receive-event."""
import json
import pathlib
import sys
import time

import pexpect
import pytest

sys.path.insert(0, str(pathlib.Path(__file__).parent))   # bench_lines.py, ws_capture.py are staged beside the test
from bench_lines import port_lines
import ws_capture

RX_BUFFER = 512
TX_BUFFER = 256
BAUDS = (1_000_000, 2_000_000)
# tests/README.ja.md 取り込みの速さ: T = 0.5 us, the bit at 2 Mbaud; 10 / T. 40 MS/s overran the probe's stream on
# V103's bench (2026-11-17: "the probe dropped a segment").
FAST_RATE = 20_000_000
# 1 Mbaud only: T = 1 us, 20 / T.
UART_RATE = 20_000_000
# 115200: T 8.7 us, 35 / T.
SLOW_RATE = 4_000_000
# The errors the sketch reports (Serial.takeErrors()): CH32RV_SERIAL_MARK_FULL.
MARK_FULL = 32


def _fnv(data):
    h = 2166136261
    for b in data:
        h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    return h


def _pattern(n):
    return bytes((i * 37 + 11) & 0xFF for i in range(n))


def _case_target():
    return json.loads(pathlib.Path(__file__).with_name("case.json").read_text())["target"]


def _expect(dut, wait, pattern, timeout=3.0):
    """The DUT's reply, looked for in short tries with wait() between so the capture keeps being read."""
    end = time.monotonic() + timeout
    while True:
        try:
            return dut.expect(pattern + rb"\r?\n", timeout=0.05)
        except pexpect.TIMEOUT:
            assert time.monotonic() < end, f"no {pattern!r}"
            wait(0.05)


def _ask(dut, wait, line, reply, timeout=3.0):
    dut.write(line + "\n")
    return _expect(dut, wait, reply, timeout)


def _ask_now(dut, line, reply, timeout=5.0):
    dut.write(line + "\n")
    return dut.expect(reply + rb"\r?\n", timeout=timeout)


def _setup(dut, request):
    """The DUT's TX / RX lines and the probe's UART on them: (target, oep_host, dut_tx, dut_rx, io, fn)."""
    from oep_client import core, fixture, host as oh, registry
    target = _case_target()
    if not target.get("upload_port", "").startswith("oep://"):
        pytest.skip("needs the probe's UART and logic capture (an OEP port)")
    oep_host = request.getfixturevalue("oep_host")
    roles = {line["role"]: line for line in port_lines(oep_host, target) if line["role"] in ("rx", "tx")}
    if set(roles) != {"rx", "tx"}:
        pytest.skip("the probe labels no <port>.rx@<pad> and <port>.tx@<pad>")
    dut_tx, dut_rx = roles["rx"], roles["tx"]          # the probe's rx hears the DUT's TX
    host = oep_host.host
    RX, TX = (registry.FIXTURE_UART.enum["role"][r] for r in ("rx", "tx"))
    for fn in core.find_all(host, "oep.fixture.uart"):
        try:
            core.plan_apply(host, [(fn, RX, dut_tx["channel"]), (fn, TX, dut_rx["channel"])])
        except oh.Rejected:
            continue
        return target, oep_host, dut_tx, dut_rx, fixture.FixtureUartIO(host, fn), fn
    raise AssertionError("no free oep.fixture.uart on the probe")


def _begin(dut, wait, baud):
    """Serial at `baud` with the DMA buffers; asserts the sizes took and the DMA requests are on."""
    m = _ask(dut, wait, f"dbegin {baud} {RX_BUFFER} {TX_BUFFER}", rb"DBEGIN (\d+) (\d+) (\d) (\d) (\d)")
    got = tuple(int(x) for x in m.groups())
    want = (RX_BUFFER, TX_BUFFER, 1, 1, 1)
    assert got == want, f"dbegin {baud}: sizes / open / DMAR / DMAT {got}, want {want}"
    return m


def _bauds(dut):
    """BAUDS the DUT's USART reaches (its clock / 16, the DUT's own answer)."""
    top = int(_ask_now(dut, "dmax", rb"DMAX (\d+)").group(1))
    return [b for b in BAUDS if b <= top]


def _read(io, wait, n, timeout):
    got = b""
    end = time.monotonic() + timeout
    while len(got) < n and time.monotonic() < end:
        got += io.read(4096)
        if len(got) < n:
            wait(0.02)
    return got


def _finish(dut, oep_host, used):
    from oep_client import core
    core.plan_release(oep_host.host, [used])
    _ask_now(dut, "devt off", rb"DEVT \d")
    _ask_now(dut, f"dbegin 115200 {RX_BUFFER} {TX_BUFFER}", rb"DBEGIN .*")


def test_uart_dma_receive(dut, ws_run, request, record_property):
    """4096 bytes at each baud of BAUDS the DUT reaches, while the DUT reads: all of them, in order, no error. The
    probe's UART sends in pieces of 256 with its own pauses; the DUT's reader keeps up, so the buffer's 512 is room
    for the reader's own delays, not for the stream. WireSkein decodes the bytes on the DUT's RX (`uart`) - what
    went in, should the DUT's count disagree."""
    from oep_client import fixture
    from wireskein.runlog import uart
    target, oep_host, dut_tx, dut_rx, io, used = _setup(dut, request)
    bauds = _bauds(dut)
    data = bytes((i * 131 + (i >> 8) * 7 + 5) & 0xFF for i in range(4096))
    got = {}

    def steps(wait):
        with ws_run.section(1, f"dma receive {dut_rx['pad']}"):
            for baud in bauds:
                _begin(dut, wait, baud)
                try:
                    probe_baud = io.configure(baud, fixture.FixtureUart.format_byte(8, "N", 1))
                except Exception as e:                    # the probe's UART refuses the rate: not the DUT's
                    got[baud] = f"skip: the probe's UART at {baud}: {e}"
                    continue
                with ws_run.section(2, f"{baud}", expect=[uart(dut_rx["pad"], probe_baud, data=data.hex())]):
                    ws_run.command(f"drecv {len(data)} 5000")
                    _ask(dut, wait, f"drecv {len(data)} 5000", rb"DRECV READY")
                    io.write(data)
                    m = _expect(dut, wait, rb"DRECV (\d+) ([0-9A-F]+) (\d+)", timeout=8)
                    ws_run.reply(m.group(0).decode())
                    got[baud] = (int(m.group(1)), m.group(2).decode(), int(m.group(3)))

    try:
        ws_capture.stream(ws_run, oep_host, [dut_rx], rate=FAST_RATE, steps=steps)
    finally:
        _finish(dut, oep_host, used)
    record_property("received", json.dumps({str(k): v for k, v in got.items()}))
    want = (len(data), f"{_fnv(data):X}", 0)
    wrong = [f"{b}: {got.get(b)}, want {want}" for b in bauds
             if not (isinstance(got.get(b), str) and got[b].startswith("skip")) and got.get(b) != want]
    assert not wrong, " | ".join(wrong)
    if all(isinstance(got.get(b), str) for b in bauds):
        pytest.skip(" | ".join(str(got[b]) for b in bauds))


def test_uart_dma_interrupts_off(dut, ws_run, request, record_property):
    """At 1 Mbaud the DUT holds interrupts off for 1 s (doff) while the probe sends. 400 bytes (4 ms on the line,
    well inside the second even with the probe's network in front): the DMA's count with interrupts still off is 400
    - they came while nothing ran - and all 400 read after, in order, no error. Without DMA the USART's one data
    register would overrun at the second byte. 640 bytes, a quarter more than the 512 of the buffer: the DMA wrote
    over the oldest 129, so the read after sees the overrun (the DMA's place moved 128 since the last look, but its
    half-transfer and transfer-complete flags both rose, and a move of 128 passes one of those points at most),
    reports UART_BUFFER_FULL_ERROR alone and keeps the newest 511 (one slot tells full from empty)."""
    from oep_client import fixture
    from wireskein.runlog import uart
    target, oep_host, dut_tx, dut_rx, io, used = _setup(dut, request)
    baud = 1_000_000
    conditions = {"inside": 400, "overrun": RX_BUFFER + RX_BUFFER // 4}
    got = {}

    def steps(wait):
        with ws_run.section(1, f"dma interrupts off {dut_rx['pad']}"):
            _begin(dut, wait, baud)
            probe_baud = io.configure(baud, fixture.FixtureUart.format_byte(8, "N", 1))
            for name, n in conditions.items():
                data = bytes((i * 29 + 3) & 0xFF for i in range(n))
                with ws_run.section(2, name, expect=[uart(dut_rx["pad"], probe_baud, data=data.hex())]):
                    ws_run.command(f"doff 1000 {n}")
                    _ask(dut, wait, f"doff 1000 {n}", rb"DOFF READY")
                    io.write(data)
                    m = _expect(dut, wait, rb"DOFF (\d+) (\d+) ([0-9A-F]+) (\d+)", timeout=8)
                    ws_run.reply(m.group(0).decode())
                    got[name] = (int(m.group(1)), int(m.group(2)), m.group(3).decode(), int(m.group(4)))

    try:
        ws_capture.stream(ws_run, oep_host, [dut_rx], rate=UART_RATE, steps=steps)
    finally:
        _finish(dut, oep_host, used)
    record_property("held", json.dumps(got))
    wrong = []
    for name, n in conditions.items():
        data = bytes((i * 29 + 3) & 0xFF for i in range(n))
        if name == "inside":
            want = (n, n, f"{_fnv(data):X}", 0)
        else:
            kept = data[-(RX_BUFFER - 1):]
            want = (len(kept), len(kept), f"{_fnv(kept):X}", MARK_FULL)
        if got.get(name) != want:
            wrong.append(f"{name}: held / read / hash / errors {got.get(name)}, want {want}")
    assert not wrong, " | ".join(wrong)


def test_uart_dma_transmit(dut, ws_run, request, record_property):
    """2048 bytes written at each baud of BAUDS the DUT reaches - eight times the 256 of the buffer, so write() waits
    for room and the DMA's runs follow one another. The probe's UART reads them all, in order, and WireSkein decodes
    them on the DUT's TX (`uart`, the DUT's baud from BRR). Time: from the first write to flush() the line needs
    2048 x 10 bits, and the sketch 2048 x one write()'s time (dwcost, measured on the DUT with Serial as it is: 50
    writes that do not wait). The DMA starts each run from write() as soon as the last one's count is 0, so the line
    waits for neither: the DUT's time is within 5 % of the longer of the two plus 1 ms (flush()'s last frame,
    micros(), the 50-write sample's spread). On the bench of 2026-11-17 the slower parts' write() was the longer (V003
    / V006 / X035 about 28 us a byte at 1 Mbaud): this case asks that the DMA adds no wait of its own, not that every
    part's write() outruns 1 Mbaud. Both write() times (with and without DMA) are recorded. Then the same with
    interrupts off around the writes (not timed: micros() stands still): every byte still goes out, write() moving the
    runs on itself."""
    from oep_client import fixture
    from wireskein.runlog import uart
    target, oep_host, dut_tx, dut_rx, io, used = _setup(dut, request)
    bauds = _bauds(dut)
    f_cpu = int(target["f_cpu"])
    n = 2048
    sent = _pattern(n)
    got = {}

    def steps(wait):
        with ws_run.section(1, f"dma transmit {dut_tx['pad']}"):
            for baud in bauds:
                _begin(dut, wait, baud)
                dut_baud = f_cpu / round(f_cpu / baud)
                try:
                    io.configure(baud, fixture.FixtureUart.format_byte(8, "N", 1))
                except Exception as e:
                    got[baud] = f"skip: the probe's UART at {baud}: {e}"
                    continue
                m = _ask(dut, wait, "dwcost", rb"DWCOST (\d+) (\d+)")
                ws_run.reply(m.group(0).decode())
                plain_ns, dma_ns = int(m.group(1)), int(m.group(2))
                for off in (0, 1):
                    name = f"{baud} {'interrupts off' if off else 'timed'}"
                    with ws_run.section(2, name, expect=[uart(dut_tx["pad"], dut_baud, data=sent.hex())]):
                        io.read(4096)                   # what came before
                        ws_run.command(f"dsend {n} {off}")
                        m = _ask(dut, wait, f"dsend {n} {off}", rb"DSEND (\d+) (\d+)", timeout=5)
                        ws_run.reply(m.group(0).decode())
                        line = _read(io, wait, n, timeout=3)
                        got[name] = dict(us=int(m.group(2)), line_us=n * 10 * 1e6 / dut_baud,
                                         write_us=n * dma_ns / 1000, plain_write_ns=plain_ns, dma_write_ns=dma_ns,
                                         ok=line == sent, received=len(line))

    try:
        ws_capture.stream(ws_run, oep_host, [dut_tx], rate=FAST_RATE, steps=steps)
    finally:
        _finish(dut, oep_host, used)
    record_property("sent", json.dumps({str(k): v for k, v in got.items()}))
    wrong = []
    for name, r in got.items():
        if isinstance(r, str):
            continue
        if not r["ok"]:
            wrong.append(f"{name}: the probe read {r['received']} bytes, not the {n} sent")
        need = max(r["line_us"], r["write_us"])
        if name.endswith("timed") and r["us"] > need * 1.05 + 1000:
            wrong.append(f"{name}: {r['us']} us, the line needs {r['line_us']:.0f} and write() {r['write_us']:.0f}")
    assert not wrong, " | ".join(wrong)
    if got and all(isinstance(r, str) for r in got.values()):
        pytest.skip(" | ".join(got.values()))


def test_uart_dma_receive_event(dut, ws_run, request, record_property):
    """onReceive() on a DMA receive buffer at 115200, as uart.receive-event: timeout-1 (the USART's IDLE) and
    timeout-10 (counted after loop(); bytes that came after the IDLE make it wait for the next) - 3 bursts of 5 bytes
    30 ms apart, 3 calls each with its burst; fifo-8 (the count, read after loop() from the DMA's place) - one burst of
    24, every call with at least 8 to read, fewer than 8 left, nothing lost."""
    from oep_client import fixture
    from wireskein.runlog import uart
    target, oep_host, dut_tx, dut_rx, io, used = _setup(dut, request)
    conditions = {
        "timeout-1": ("1 1 32", [5, 5, 5]),
        "timeout-10": ("1 10 32", [5, 5, 5]),
        "fifo-8": ("0 0 8", [24]),
    }
    results = {}

    def steps(wait):
        with ws_run.section(1, f"dma receive events {dut_rx['pad']}"):
            _begin(dut, wait, 115200)
            probe_baud = io.configure(115200, fixture.FixtureUart.format_byte(8, "N", 1))
            for name, (args, bursts) in conditions.items():
                data = bytes((i * 29 + 3) & 0xFF for i in range(sum(bursts)))
                with ws_run.section(2, name, expect=[uart(dut_rx["pad"], probe_baud, data=data.hex())]):
                    _ask(dut, wait, f"devt {args}", rb"DEVT 1")
                    at = 0
                    for k in bursts:
                        io.write(data[at:at + k])
                        at += k
                        wait(0.03)
                    m = _ask(dut, wait, "devq", rb"DEVQ (\d+) (\d+) (\d+) ([0-9A-F]+) ([0-9,]*)")
                    ws_run.reply(m.group(0).decode())
                    seen = [int(x) for x in m.group(5).decode().split(",") if x]
                    results[name] = dict(calls=int(m.group(1)), read=int(m.group(2)), left=int(m.group(3)),
                                         hash=m.group(4).decode(), seen=seen, want=f"{_fnv(data):X}")

    try:
        ws_capture.stream(ws_run, oep_host, [dut_rx], rate=SLOW_RATE, steps=steps)
    finally:
        _finish(dut, oep_host, used)
    record_property("events", json.dumps(results))
    wrong = []
    for name, (_, bursts) in conditions.items():
        r = results.get(name)
        if r is None:
            wrong.append(f"{name}: not run")
        elif r["hash"] != r["want"] or r["read"] + r["left"] != sum(bursts):
            wrong.append(f"{name}: {r['read']} read + {r['left']} left, hash {r['hash']} (want {sum(bursts)}, {r['want']})")
        elif name.startswith("timeout") and (r["calls"] != len(bursts) or r["seen"] != bursts or r["left"]):
            wrong.append(f"{name}: calls {r['calls']} saw {r['seen']} left {r['left']}, want {bursts}")
        elif name.startswith("fifo") and (not r["calls"] or min(r["seen"]) < 8 or r["left"] >= 8):
            wrong.append(f"{name}: calls {r['calls']} saw {r['seen']} left {r['left']}, want each >= 8, < 8 left")
    assert not wrong, " | ".join(wrong)
