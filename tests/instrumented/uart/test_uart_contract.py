"""uart.external-round-trip: default USART route, 115200 8N1, binary 64-byte payload; then stdio on
the same port: puts() reaches Serial once it is begun, ch32rv_set_stdout(nullptr) discards, and
ch32rv_set_stdout(&Serial) brings it back.

uart.output-only (OEP ports with <port>.rx@<pad>): the formats' conditions with no UART on the probe, the DUT's TX
only, decoded the same way - the baseline. uart.formats (OEP ports with <port>.rx@<pad> and <port>.tx@<pad> labelled): 9600, 115200 and 1 Mbaud at 8N1, and
8E1, 8O1, 8N2, 7E1, 7O1 at 115200, both ways, in one capture of the two lines with a WireSkein section per
condition (both lines idle at its start and end): the DUT sends 16 bytes (decoded on its TX: the bytes, the format, the bit time of the DUT's
Serial.baudRate() (its USART clock / BRR) within 3 %,
no framing or parity error) and receives 16 the probe's UART sends (decoded on its RX the same way, and the DUT's
count and FNV-1a hash of what it read); the other line stays idle meanwhile.

uart.receive-errors (the same labels): the probe's UART set off from the DUT's format on purpose, and what
Serial.onReceiveError() reports compared with the kind made (test_uart_receive_errors).

The modes beyond plain UART, each skipped with its reason where the bench lacks the lines (plain `<port>@<pad>` lines
besides the rx / tx labels): uart.flow-control (test_uart_flow_control: Serial's CTS / RTS pads among the plain
lines), uart.rs485 (a plain line as DE), uart.half-duplex (the DUT's TX pad alone), uart.receive-event (onReceive())
and uart.software-serial (two plain lines). The DUT runs sketch_support/uart/uart.ino."""
import secrets


def test_uart_contract(dut, request):
    # The probe's UART: oep-client's oep_uart on an OEP port, the ch32rv plugin's ch32_uart through a WCH-Link.
    # --port is the plugin's monitor wrapping by now; --flash-port keeps the address as given.
    address = request.config.getoption("flash_port", None) or request.config.getoption("port", None) or ""
    oep = "oep://" in str(address)
    link = None
    if not oep:
        # A WCH-Link port's record says which pads its UART reaches (tests/RECORDS.ja.md link_uart): none, the
        # DUT's TX alone, or pads other than Serial's default (nanoCH32H417's LinkE: PA9 / PA10).
        target = json.loads(pathlib.Path(__file__).with_name("case.json").read_text())["target"]
        link = target.get("record", {}).get("link_uart")
        if link == {}:
            pytest.skip("the record says the WCH-Link's UART reaches no DUT pad (link_uart {})")
        if link and "tx" in link and "rx" in link:
            pin = lambda p: ((ord(p[1].upper()) - ord("A") + 2) << 5) | int(p[2:])   # noqa: E731 - "pa9" -> PA9
            rx, tx = pin(link["rx"]), pin(link["tx"])
            dut.write(f"upins {rx} {tx}\n")
            assert dut.expect(rb"UPINS (\d)\r?\n", timeout=10).group(1) == b"1", \
                f"Serial refused {link['tx']} / {link['rx']} (the record's link_uart)"
    if oep:
        uart = request.getfixturevalue("oep_uart").open(115200)
    else:
        # Opened at another rate first: the line coding's change makes the WCH-Link rebuild its UART bridge. The
        # CH549 Links (3-1, 4-3) had got into handing the DUT's bytes to USB 79 bytes late, always - what came was
        # the bytes of an earlier run, and the echo never arrived (2026-11-14); a change of rate cleared it.
        request.getfixturevalue("ch32_uart").open(9600)
        uart = request.getfixturevalue("ch32_uart").open(115200)
    if link is not None and "rx" not in link:
        # The DUT's TX alone reaches the Link (M030's motor board): what the DUT sends arrives whole.
        dut.write("usend 64\n")
        uart.expect_exact(bytes((i * 37 + 11) & 0xFF for i in range(64)), timeout=10)
        dut.expect_exact("USEND 64", timeout=10)
        return
    payload = bytes([0, 255, 85, 170]) + secrets.token_bytes(60)
    dut.write("uart\n")
    dut.expect_exact("UART READY", timeout=15)
    uart.write(payload)
    uart.expect_exact(payload, timeout=10)
    dut.expect_exact("UART COUNT 64", timeout=10)
    dut.write("stdio\n")
    dut.expect_exact("STDIO DONE", timeout=10)
    m = uart.expect(rb"(?s)STDIO SERIAL\r?\n(.*?)STDIO BACK", timeout=10)
    assert b"DISCARDED" not in m.group(1), m.group(1)


# tests/README.ja.md 取り込みの速さ: T = 1 us, the bit at the fastest baud (1 Mbaud), 20 / T.
UART_RATE = 20_000_000

# ---- uart.formats: every baud and format the condition list names, both directions, judged by WireSkein ----------
import json
import pathlib
import sys
import time

import pexpect
import pytest

sys.path.insert(0, str(pathlib.Path(__file__).parent))   # bench_lines.py, ws_capture.py are staged beside the test
from bench_lines import port_lines
import ws_capture

# (baud, format index in the sketch's ucfg, data bits, parity, stop bits): the driver's branches - BRR at a low, the
# usual and a high rate, then parity (the ninth bit with 8 data bits), two stop bits and 7 data bits at 115200.
FORMATS = {0: (8, "none", 1), 1: (8, "even", 1), 2: (8, "odd", 1), 3: (8, "none", 2), 4: (7, "even", 1), 5: (7, "odd", 1)}
CONDITIONS = [(9600, 0), (115200, 0), (1000000, 0), (115200, 1), (115200, 2), (115200, 3), (115200, 4), (115200, 5)]
COUNT = 16


def _fnv(data):
    h = 2166136261
    for b in data:
        h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    return h


def test_uart_output_only(dut, ws_run, request, record_property):
    """Every condition of uart.formats with no UART on the probe: it only captures the DUT's TX, so what is seen is
    the DUT's own (the baseline when the round trip looks wrong). The DUT sends 16 bytes in each; decoded on its TX
    (the bytes, the format, the bit time of the DUT's Serial.baudRate() (its USART clock / BRR) within 3 %, no
    framing or parity error), idle at the section's
    start and end."""
    from wireskein.runlog import ends, starts, uart
    case = json.loads(pathlib.Path(__file__).with_name("case.json").read_text())
    target = case["target"]
    if not target.get("upload_port", "").startswith("oep://"):
        pytest.skip("the formats need the probe's logic capture (an OEP port)")
    oep_host = request.getfixturevalue("oep_host")
    roles = {line["role"]: line for line in port_lines(oep_host, target) if line["role"] in ("rx", "tx")}
    if "rx" not in roles:
        pytest.skip("the probe labels no <port>.rx@<pad> (the DUT's TX)")
    dut_tx = roles["rx"]
    sent = bytes((i * 37 + 11) & 0xFF for i in range(COUNT))

    def steps(wait):
        idle = {dut_tx["pad"]: 1}
        with ws_run.section(1, f"uart output only {dut_tx['pad']}"):
            for baud, k in CONDITIONS:
                bits, parity, stop = FORMATS[k]
                name = f"{baud} {bits}{parity[0].upper()}{stop}"
                out = bytes(b & ((1 << bits) - 1) for b in sent)
                with ws_run.section(2, name, expect=[starts(idle), ends(idle)]):
                    ws_run.command(f"ucfg {baud} {k}")
                    dut.write(f"ucfg {baud} {k}\n")
                    dut_baud = int(_expect(dut, wait, rb"UCFG 1 (\d+)").group(1))   # what BRR makes of it
                    wait(0.02)
                    with ws_run.section(3, "dut sends", expect=[uart(dut_tx["pad"], dut_baud,
                                                                     data=out.hex(), bits=bits, parity=parity,
                                                                     stop=stop)]):
                        ws_run.command(f"usend {COUNT}")
                        dut.write(f"usend {COUNT}\n")
                        _expect(dut, wait, rb"USEND \d+")
                        wait(0.02 + 12 * COUNT / baud)

    try:
        ws_capture.stream(ws_run, oep_host, [dut_tx], rate=UART_RATE, steps=steps)
    finally:
        dut.write("ucfg 115200 0\n")
        dut.expect(rb"UCFG \d \d+\r?\n", timeout=5)


def _expect(dut, wait, pattern, timeout=3.0):
    """The DUT's reply, looked for in short tries with wait() between so the stream keeps being read."""
    end = time.monotonic() + timeout
    while True:
        try:
            return dut.expect(pattern + rb"\r?\n", timeout=0.05)
        except pexpect.TIMEOUT:
            assert time.monotonic() < end, f"no {pattern!r}"
            wait(0.05)


def _probe_uart(oep_host, target):
    """The DUT's TX and RX lines and the probe's UART on them: (dut_tx, dut_rx, io, fn); skips without both labels."""
    from oep_client import core, fixture, host as oh, registry
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
        return dut_tx, dut_rx, fixture.FixtureUartIO(host, fn), fn
    raise AssertionError("no free oep.fixture.uart on the probe")


def test_uart_formats(dut, ws_run, request, record_property):
    from oep_client import core, fixture
    case = json.loads(pathlib.Path(__file__).with_name("case.json").read_text())
    target = case["target"]
    if not target.get("upload_port", "").startswith("oep://"):
        pytest.skip("the formats need the probe's logic capture (an OEP port)")
    oep_host = request.getfixturevalue("oep_host")
    dut_tx, dut_rx, io, used = _probe_uart(oep_host, target)
    host = oep_host.host
    sent = bytes((i * 37 + 11) & 0xFF for i in range(COUNT))
    hashes = {}

    def ask(wait, line, reply, timeout=3.0):
        dut.write(line + "\n")
        end = time.monotonic() + timeout
        while True:
            try:
                return dut.expect(reply + rb"\r?\n", timeout=0.05)
            except pexpect.TIMEOUT:
                if time.monotonic() > end:
                    raise
                wait(0.05)

    def steps(wait):
        from wireskein.runlog import ends, level, only_moving, starts, uart
        with ws_run.section(1, f"uart {dut_tx['pad']} / {dut_rx['pad']}", expect=[only_moving([dut_tx["pad"], dut_rx["pad"]])]):
            for baud, k in CONDITIONS:
                bits, parity, stop = FORMATS[k]
                name = f"{baud} {bits}{parity[0].upper()}{stop}"
                mask = (1 << bits) - 1
                out = bytes(b & mask for b in sent)
                back = bytes(b & mask for b in reversed(sent))
                idle = {dut_tx["pad"]: 1, dut_rx["pad"]: 1}
                with ws_run.section(2, name, expect=[starts(idle), ends(idle)]):
                    ws_run.command(f"ucfg {baud} {k}")
                    m = ask(wait, f"ucfg {baud} {k}", rb"UCFG (\d) (\d+)")
                    assert m.group(1) == b"1", f"{name}: Serial.begin refused"
                    dut_baud = int(m.group(2))                 # what BRR makes of it (Serial.baudRate())
                    probe_baud = io.configure(baud, fixture.FixtureUart.format_byte(bits, parity[0].upper(), stop))
                    wait(0.02)
                    common = dict(bits=bits, parity=parity, stop=stop)
                    with ws_run.section(3, "dut sends", expect=[uart(dut_tx["pad"], dut_baud, data=out.hex(), **common),
                                                                level(dut_rx["pad"], 1)]):
                        ws_run.command(f"usend {COUNT}")
                        ask(wait, f"usend {COUNT}", rb"USEND \d+")
                        wait(0.02 + 12 * COUNT / baud)
                        io.read(4096)                           # what the probe's UART took in, not judged here
                    with ws_run.section(3, "dut receives",
                                        expect=[uart(dut_rx["pad"], probe_baud, data=back.hex(), **common),
                                                level(dut_tx["pad"], 1)]):
                        ws_run.command(f"urecv {COUNT}")
                        dut.write(f"urecv {COUNT} {int(200 + 12000 * COUNT / baud)}\n")
                        dut.expect(rb"URECV READY\r?\n", timeout=3)
                        io.write(back)
                        m = None
                        end = time.monotonic() + 3
                        while m is None:
                            try:
                                m = dut.expect(rb"URECV (\d+) ([0-9A-F]+)\r?\n", timeout=0.05)
                            except pexpect.TIMEOUT:
                                assert time.monotonic() < end, f"{name}: no URECV"
                                wait(0.05)
                        ws_run.reply(m.group(0).decode())
                        hashes[name] = (int(m.group(1)), m.group(2).decode())
                        wait(0.02)

    try:
        ws_capture.stream(ws_run, oep_host, [dut_tx, dut_rx], rate=UART_RATE, steps=steps)
    finally:
        core.plan_release(host, [used])
        dut.write("ucfg 115200 0\n")
        dut.expect(rb"UCFG \d \d+\r?\n", timeout=5)
    record_property("received", json.dumps(hashes))
    for baud, k in CONDITIONS:
        bits, parity, stop = FORMATS[k]
        name = f"{baud} {bits}{parity[0].upper()}{stop}"
        back = bytes(b & ((1 << bits) - 1) for b in reversed(sent))
        assert hashes.get(name) == (COUNT, f"{_fnv(back):X}"), f"{name}: the DUT received {hashes.get(name)}"


# ---- uart.receive-errors: Serial.onReceiveError() for each kind the probe can make on purpose --------------------
# hardwareSerial_error_t's numbers (ESP32's): the sketch's uerr reports bit k for kind k.
BREAK, BUFFER_FULL, FIFO_OVF, FRAME, PARITY = 1, 2, 3, 4, 5
RX_BUFFER = 64                     # CH32RV_SERIAL_RX_BUFFER_SIZE's default
EVEN_NONZERO = bytes(b for b in range(1, 256) if bin(b).count("1") % 2 == 0)
# What WireSkein finds on the DUT's RX read in the DUT's own format (exactly; the rest none): every 8O1 byte a parity
# error to 8E1, every even-parity 8E1 byte a framing error to 8N1 (the parity bit 0 where the stop bit should be), and
# the four 0x00 four framing errors - a break to the core, which WireSkein reads as data 0 with no stop bit.
WIRE_ERRORS = {"parity": {"parity": COUNT}, "frame": {"framing": COUNT}, "break": {"framing": 4}}


def test_uart_receive_errors(dut, ws_run, request, record_property):
    """Each condition sets the DUT's Serial at 115200 and the probe's UART to a format, then the probe sends: what
    goes on the DUT's RX is decoded by WireSkein in the probe's format (the bytes, no error of that format's own), and
    what the DUT made of it - how many bytes it kept and which kinds onReceiveError() reported (uerr) - is compared
    here; the same line is read by WireSkein in the DUT's format too, with the errors the DUT should see there
    (WIRE_ERRORS). clean: 8N1 both, nothing reported, every byte kept. parity: 8E1 on the DUT, 8O1 sent, every byte dropped
    with UART_PARITY_ERROR. frame: 8N1 on the DUT, 8E1 sent with even-parity bytes, so the DUT samples the parity
    bit (0) as its stop bit: UART_FRAME_ERROR, every byte dropped. break: the same with 0x00, the line low for the
    whole frame: UART_BREAK_ERROR alone. buffer-full: 100 bytes while the sketch reads none, the buffer keeps
    RX_BUFFER - 1 (one slot tells full from empty) and UART_BUFFER_FULL_ERROR. overrun: interrupts off while 16
    bytes come: UART_FIFO_OVF_ERROR."""
    from oep_client import core, fixture
    from wireskein.runlog import ends, starts, uart
    case = json.loads(pathlib.Path(__file__).with_name("case.json").read_text())
    target = case["target"]
    if not target.get("upload_port", "").startswith("oep://"):
        pytest.skip("the receive errors need the probe's UART and logic capture (an OEP port)")
    oep_host = request.getfixturevalue("oep_host")
    dut_tx, dut_rx, io, used = _probe_uart(oep_host, target)
    host = oep_host.host
    baud = 115200
    clean = bytes((i * 37 + 11) & 0xFF for i in range(COUNT))
    # name: (DUT format index, probe format, bytes, how the DUT waits, bytes it keeps, kinds reported)
    conditions = {
        "clean": (0, (8, "none", 1), clean, "hold", COUNT, set()),
        "parity": (1, (8, "odd", 1), clean, "hold", 0, {PARITY}),
        "frame": (0, (8, "even", 1), EVEN_NONZERO[:COUNT], "hold", 0, {FRAME}),
        "break": (0, (8, "even", 1), bytes(4), "hold", 0, {BREAK}),
        "buffer-full": (0, (8, "none", 1), bytes((i * 37 + 11) & 0xFF for i in range(100)), "hold", RX_BUFFER - 1,
                        {BUFFER_FULL}),
        "overrun": (0, (8, "none", 1), clean, "off", None, {FIFO_OVF}),
    }
    got = {}

    def steps(wait):
        idle = {dut_tx["pad"]: 1, dut_rx["pad"]: 1}
        with ws_run.section(1, f"receive errors {dut_rx['pad']}"):
            for name, (k, (bits, parity, stop), data, how, _, _) in conditions.items():
                dut_bits, dut_parity, dut_stop = FORMATS[k]
                wire_errors = WIRE_ERRORS.get(name)
                with ws_run.section(2, name):
                    ws_run.command(f"ucfg {baud} {k}")
                    assert _expect_after(dut, wait, f"ucfg {baud} {k}", rb"UCFG (\d) \d+").group(1) == b"1"
                    probe_baud = io.configure(baud, fixture.FixtureUart.format_byte(bits, parity[0].upper(), stop))
                    _expect_after(dut, wait, "uerr", rb"UERR \d+")          # registers, clears what came before
                    wait(0.02)
                    with ws_run.section(3, "probe sends",
                                        expect=[uart(dut_rx["pad"], probe_baud, data=data.hex(), bits=bits,
                                                     parity=parity, stop=stop),
                                                # the same line in the DUT's format: the errors it should see
                                                uart(dut_rx["pad"], probe_baud, bits=dut_bits, parity=dut_parity,
                                                     stop=dut_stop, errors=wire_errors),
                                                starts(idle), ends(idle)]):
                        if how == "hold":
                            ws_run.command("uhold 500")
                            _expect_after(dut, wait, "uhold 500", rb"UHOLD READY")
                            io.write(data)
                            m = _expect(dut, wait, rb"UHOLD (\d+)")
                            kept = int(m.group(1))
                        else:
                            ws_run.command("uoff 100")
                            _expect_after(dut, wait, "uoff 100", rb"UOFF READY")
                            io.write(data)
                            m = _expect(dut, wait, rb"UOFF")
                            kept = None
                        ws_run.reply(m.group(0).decode())
                        wait(0.02)
                    m = _expect_after(dut, wait, "uerr", rb"UERR (\d+)")
                    ws_run.reply(m.group(0).decode())
                    mask = int(m.group(1))
                    got[name] = (kept, {kind for kind in range(8) if mask >> kind & 1})

    try:
        ws_capture.stream(ws_run, oep_host, [dut_tx, dut_rx], rate=UART_RATE, steps=steps)
    finally:
        core.plan_release(host, [used])
        dut.write("ucfg 115200 0\n")
        dut.expect(rb"UCFG \d \d+\r?\n", timeout=5)
    record_property("received", json.dumps({n: [kept, sorted(kinds)] for n, (kept, kinds) in got.items()}))
    wrong = []
    for name, (*_, kept, kinds) in conditions.items():
        if name not in got:
            wrong.append(f"{name}: not run")
            continue
        if got[name][1] != kinds or (kept is not None and got[name][0] != kept):
            wrong.append(f"{name}: kept {got[name][0]} reported {sorted(got[name][1])}, want {kept} {sorted(kinds)}")
    assert not wrong, " | ".join(wrong)


def _expect_after(dut, wait, line, pattern, timeout=3.0):
    dut.write(line + "\n")
    return _expect(dut, wait, pattern, timeout)


# ---- the modes beyond plain UART, and SoftwareSerial (tests/README.ja.md: uart.flow-control, uart.rs485,
# uart.half-duplex, uart.receive-event, uart.software-serial) --------------------------------------------------------
# tests/README.ja.md 取り込みの速さ: these run at 115200 and below (RS485 also at 1 Mbaud): T is the bit there.
SLOW_RATE = 4_000_000          # 115200: T 8.7 us, 35 / T; DE / RTS / CTS times are judged in tens of microseconds
HALF_DUPLEX_RATE = 1_000_000   # 9600 (an open-drain line with the probe's pull-up): T 104 us, 100 / T


def _pattern(n, bits=8):
    return bytes(((i * 37 + 11) & 0xFF) & ((1 << bits) - 1) for i in range(n))


def _case_target():
    return json.loads(pathlib.Path(__file__).with_name("case.json").read_text())["target"]


def _oep_only(request):
    target = _case_target()
    if not target.get("upload_port", "").startswith("oep://"):
        pytest.skip("needs the probe's lines and logic capture (an OEP port)")
    return target, request.getfixturevalue("oep_host")


def _serial_lines(oep_host, target):
    roles = {line["role"]: line for line in port_lines(oep_host, target) if line["role"] in ("rx", "tx")}
    if set(roles) != {"rx", "tx"}:
        pytest.skip("the probe labels no <port>.rx@<pad> and <port>.tx@<pad>")
    return roles["rx"], roles["tx"]          # the DUT's TX (the probe's rx), the DUT's RX


def _plain_lines(oep_host, target, *taken):
    """The port's `<port>@<pad>` lines (no role), not the chip's debug pads nor a pad already used."""
    debug = set(target.get("debug_pads") or [])
    used = {line["pad"] for line in taken}
    return [line for line in port_lines(oep_host, target)
            if line["role"] is None and line["pad"] not in debug and line["pad"] not in used]


def _plan_uart(oep_host, rx=None, tx=None):
    """A free oep.fixture.uart with RX on line `rx` and / or TX on line `tx`: (io, fn); None when none takes them."""
    from oep_client import core, fixture, host as oh, registry
    host = oep_host.host
    RX, TX = (registry.FIXTURE_UART.enum["role"][r] for r in ("rx", "tx"))
    plan = [(RX, rx["channel"])] if rx else []
    plan += [(TX, tx["channel"])] if tx else []
    for fn in core.find_all(host, "oep.fixture.uart"):
        try:
            core.plan_apply(host, [(fn, role, channel) for role, channel in plan])
        except oh.Rejected:
            continue
        return fixture.FixtureUartIO(host, fn), fn
    return None, None


def _ask(dut, wait, line, reply, timeout=3.0):
    dut.write(line + "\n")
    return _expect(dut, wait, reply, timeout)


def _ask_now(dut, line, reply, timeout=5.0):
    dut.write(line + "\n")
    return dut.expect(reply + rb"\r?\n", timeout=timeout)


def test_uart_flow_control(dut, ws_run, request, record_property):
    """setHwFlowCtrlMode() on Serial's route, with its CTS / RTS pads found among the probe's plain lines (the DUT
    accepts a pad only where it is the USART's: uflow). CTS: the probe holds the line high, the DUT writes one byte -
    TX stays idle; the probe lets CTS low and the DUT sends 15 more: all 16 on TX (WireSkein `uart`). RTS (threshold
    16): the DUT reads nothing for 800 ms while the probe sends 32 bytes regardless (its UART has no CTS): RTS is
    high from the 17th byte (it waits in the data register) until the DUT reads, then low again (WireSkein `level`,
    `becomes`, `starts`, `ends`; before the hold RTS also pulses for each byte until the interrupt takes it, some
    10 us); the DUT held 16 and read 17, the first 17 sent (the rest overran)."""
    from oep_client import core
    from oep_client.fixture import Gpio
    from wireskein.runlog import becomes, ends, level, starts, uart
    target, oep_host = _oep_only(request)
    dut_tx, dut_rx = _serial_lines(oep_host, target)
    host = oep_host.host
    plain = _plain_lines(oep_host, target, dut_tx, dut_rx)
    dut_baud = int(_ask_now(dut, "ucfg 115200 0", rb"UCFG \d (\d+)").group(1))
    cts = next((p for p in plain if _ask_now(dut, f"uflow cts {p['pin']}", rb"UFLOW (\d)").group(1) == b"1"), None)
    rts = next((p for p in plain if p is not cts
                and _ask_now(dut, f"uflow rts {p['pin']} 16", rb"UFLOW (\d)").group(1) == b"1"), None)
    _ask_now(dut, "uflow off", rb"UFLOW \d")
    record_property("cts", cts["pad"] if cts else "-")
    record_property("rts", rts["pad"] if rts else "-")
    if not cts and not rts:
        pytest.skip("the probe labels no <port>@<pad> on Serial's CTS or RTS pad (uflow refused every plain line)")
    io, used = _plan_uart(oep_host, rx=dut_tx, tx=dut_rx)
    assert io is not None, "no free oep.fixture.uart on the probe"
    gpio = Gpio(host) if cts else None
    if gpio:
        core.plan_apply(host, [(gpio.fn, 1, cts["channel"])])
        gpio.configure(cts["channel"], Gpio.OUTPUT_HIGH)
    baud = 115200
    probe_baud = io.configure(baud)
    sent = bytes((i * 53 + 7) & 0xFF for i in range(32))
    got = {}

    def steps(wait):
        if cts:
            assert _ask(dut, wait, f"uflow cts {cts['pin']}", rb"UFLOW (\d)").group(1) == b"1"
            with ws_run.section(1, f"cts {cts['pad']}"):
                with ws_run.section(2, "cts high: nothing goes out", expect=[level(dut_tx["pad"], 1)]):
                    ws_run.command("uwrite 1")
                    _ask(dut, wait, "uwrite 1", rb"UWRITE 1")
                    wait(0.05)
                with ws_run.section(2, "cts low: the bytes go",
                                    expect=[becomes(cts["pad"], 0),
                                            uart(dut_tx["pad"], dut_baud, data=(_pattern(1) + _pattern(15)).hex())]):
                    gpio.configure(cts["channel"], Gpio.OUTPUT_LOW)
                    wait(0.01)
                    ws_run.command("usend 15")
                    _ask(dut, wait, "usend 15", rb"USEND \d+")
                    wait(0.02)
            gpio.configure(cts["channel"], Gpio.OUTPUT_HIGH)
            _ask(dut, wait, "uflow off", rb"UFLOW \d")
        if rts:
            assert _ask(dut, wait, f"uflow rts {rts['pin']} 16", rb"UFLOW (\d)").group(1) == b"1"
            # RTS also rises for every byte while it waits in the data register for the interrupt (some 10 us): the
            # hold is RTS high for good from the 17th byte until the DUT reads, and low again after.
            with ws_run.section(1, f"rts {rts['pad']}", expect=[starts({rts["pad"]: 0}), ends({rts["pad"]: 0})]):
                ws_run.command("urts 800")
                _ask(dut, wait, "urts 800", rb"URTS READY")
                with ws_run.section(2, "probe sends 32", expect=[uart(dut_rx["pad"], probe_baud, data=sent.hex())]):
                    io.write(sent)
                    wait(0.1)
                with ws_run.section(2, "held: rts high", expect=[level(rts["pad"], 1)]):
                    wait(0.2)
                with ws_run.section(2, "read: rts low again", expect=[becomes(rts["pad"], 0)]):
                    m = _expect(dut, wait, rb"URTS (\d+) (\d+) ([0-9A-F]+)", timeout=3)
                    ws_run.reply(m.group(0).decode())
                    got["rts"] = (int(m.group(1)), int(m.group(2)), m.group(3).decode())
            _ask(dut, wait, "uflow off", rb"UFLOW \d")

    lines = [dut_tx, dut_rx] + [p for p in (cts, rts) if p]
    try:
        ws_capture.stream(ws_run, oep_host, lines, rate=SLOW_RATE, steps=steps)
    finally:
        if gpio:
            gpio.configure(cts["channel"], Gpio.INPUT)
            core.plan_release(host, [gpio.fn])
        core.plan_release(host, [used])
        _ask_now(dut, "uflow off", rb"UFLOW \d")
        _ask_now(dut, "ucfg 115200 0", rb"UCFG \d \d+")
    if rts:
        record_property("rts_result", json.dumps(got.get("rts")))
        assert got.get("rts") == (16, 17, f"{_fnv(sent[:17]):X}"), \
            f"RTS: held / read / hash {got.get('rts')}, want 16 held, the first 17 read"


def test_uart_rs485(dut, ws_run, request, record_property):
    """setMode(UART_MODE_RS485_HALF_DUPLEX) with the driver enable on a plain line (any pad: the core drives it). At
    115200 and 1 Mbaud the DUT writes 4 bytes, waits 2 ms and writes 12: WireSkein's `rs485` reads TX and DE - every
    character inside a window of DE high, DE up at most 20 us before the first start bit and down at most 20 us after
    the last stop bit (never inside it), the bytes in order; DE low at the section's start and end."""
    from wireskein.runlog import ends, rs485, starts
    target, oep_host = _oep_only(request)
    dut_tx, dut_rx = _serial_lines(oep_host, target)
    plain = _plain_lines(oep_host, target, dut_tx, dut_rx)
    if not plain:
        pytest.skip("the probe labels no plain <port>@<pad> line for the driver enable")
    de = plain[0]
    record_property("de", de["pad"])

    def steps(wait):
        with ws_run.section(1, f"rs485 de {de['pad']}"):
            for baud in (115200, 1000000):
                idle = {de["pad"]: 0, dut_tx["pad"]: 1}
                m = _ask(dut, wait, f"ucfg {baud} 0", rb"UCFG (\d) (\d+)")
                assert m.group(1) == b"1"
                dut_baud = int(m.group(2))
                assert _ask(dut, wait, f"u485 {de['pin']}", rb"U485 (\d)").group(1) == b"1", "setMode refused"
                wait(0.01)
                with ws_run.section(2, f"{baud} 8N1",
                                    expect=[rs485(dut_tx["pad"], de["pad"], dut_baud, data=_pattern(16).hex(),
                                                  lead_max_s=20e-6, tail_max_s=20e-6),
                                            starts(idle), ends(idle)]):
                    ws_run.command("ude 4 2 12")
                    _ask(dut, wait, "ude 4 2 12", rb"UDE")
                    wait(0.01)

    try:
        ws_capture.stream(ws_run, oep_host, [dut_tx, de], rate=UART_RATE, steps=steps)
    finally:
        _ask_now(dut, "u485 off", rb"U485 \d")
        _ask_now(dut, "ucfg 115200 0", rb"UCFG \d \d+")


def test_uart_half_duplex(dut, ws_run, request, record_property):
    """Single-wire half duplex (setPins(tx, tx): HDSEL) at 9600 on the DUT's TX pad, the probe's input pull-up on it
    (the pad is open drain). The DUT sends 16 bytes: decoded on the line, and nothing of them in the DUT's own receive
    buffer (the USART does not receive while it sends - unlike ESP32's one-wire; every family on the bench,
    2026-11-17). Then the probe's UART TX on that line sends 16 and the DUT receives them - not
    where the pads have no open drain (X035 / X033: UHD's second number 0), where the DUT drives the idle line and
    the two outputs would fight."""
    from oep_client import core
    from oep_client.fixture import Gpio
    from wireskein.runlog import ends, starts, uart
    target, oep_host = _oep_only(request)
    dut_tx, dut_rx = _serial_lines(oep_host, target)
    host = oep_host.host
    baud = 9600
    gpio = Gpio(host)
    core.plan_apply(host, [(gpio.fn, 1, dut_tx["channel"])])
    gpio.configure(dut_tx["channel"], Gpio.INPUT_PULLUP)
    m = _ask_now(dut, f"uhd {dut_tx['pin']}", rb"UHD (\d) (\d) (\d+)")
    dut_baud = int(m.group(3))
    if m.group(1) != b"1":
        core.plan_release(host, [gpio.fn])
        raise AssertionError(f"Serial.begin(9600, SERIAL_8N1, {dut_tx['pad']}, {dut_tx['pad']}) refused")
    open_drain = m.group(2) == b"1"
    record_property("open_drain", open_drain)
    out, back = _pattern(16), bytes(reversed(_pattern(16)))
    got = {}
    used = None

    def steps(wait):
        nonlocal used
        idle = {dut_tx["pad"]: 1}
        with ws_run.section(1, f"one wire {dut_tx['pad']}"):
            with ws_run.section(2, "dut sends", expect=[uart(dut_tx["pad"], dut_baud, data=out.hex()), starts(idle),
                                                         ends(idle)]):
                ws_run.command("usend 16")
                _ask(dut, wait, "usend 16", rb"USEND \d+", timeout=5)
                wait(0.02)
            m = _ask(dut, wait, "uavail", rb"UAVAIL (\d+) ([0-9A-F]+)")
            ws_run.reply(m.group(0).decode())
            got["echo"] = (int(m.group(1)), m.group(2).decode())
            if not open_drain:
                return
            gpio.configure(dut_tx["channel"], Gpio.INPUT)
            core.plan_release(host, [gpio.fn])
            io, used = _plan_uart(oep_host, tx=dut_tx)
            if io is None:
                got["receive"] = "no oep.fixture.uart takes TX alone on the line"
                return
            probe_baud = io.configure(baud)
            wait(0.02)
            with ws_run.section(2, "dut receives", expect=[uart(dut_tx["pad"], probe_baud, data=back.hex()),
                                                            starts(idle), ends(idle)]):
                ws_run.command("urecv 16")
                dut.write("urecv 16 3000\n")
                _expect(dut, wait, rb"URECV READY")
                io.write(back)
                m = _expect(dut, wait, rb"URECV (\d+) ([0-9A-F]+)", timeout=5)
                ws_run.reply(m.group(0).decode())
                got["receive"] = (int(m.group(1)), m.group(2).decode())
                wait(0.02)

    try:
        ws_capture.stream(ws_run, oep_host, [dut_tx], rate=HALF_DUPLEX_RATE, steps=steps)
    finally:
        core.plan_release(host, [f for f in (gpio.fn, used) if f is not None])
        _ask_now(dut, f"upins {dut_rx['pin']} {dut_tx['pin']}", rb"UPINS \d")
        _ask_now(dut, "ucfg 115200 0", rb"UCFG \d \d+")
    record_property("received", json.dumps(got))
    assert got.get("echo") == (0, f"{_fnv(b''):X}"), f"what the DUT sent came back to it: {got.get('echo')}"
    if open_drain:
        assert got.get("receive") == (16, f"{_fnv(back):X}"), f"what the probe sent: {got.get('receive')}"
    else:
        record_property("receive", "not run: the pads have no open drain")


def test_uart_receive_event(dut, ws_run, request, record_property):
    """Serial.onReceive() at 115200. timeout-1 (onlyOnTimeout, setRxTimeout(1): the USART's IDLE) and timeout-10
    (10 symbols, counted at delivery): the probe sends 3 bursts of 5 bytes 30 ms apart - 3 calls, each with the 5
    bytes of its burst to read. fifo-8 (setRxFIFOFull(8), setRxTimeout(0)): one burst of 24 - every call with at
    least 8 to read, fewer than 8 left over, nothing lost. The bursts are decoded on the DUT's RX (WireSkein `uart`):
    a gap inside a burst would make a timeout of its own."""
    from wireskein.runlog import uart
    target, oep_host = _oep_only(request)
    dut_tx, dut_rx = _serial_lines(oep_host, target)
    io, used = _plan_uart(oep_host, rx=dut_tx, tx=dut_rx)
    assert io is not None, "no free oep.fixture.uart on the probe"
    host = oep_host.host
    probe_baud = io.configure(115200)
    # name: (uevt arguments, bursts)
    conditions = {
        "timeout-1": ("1 1 32", [5, 5, 5]),
        "timeout-10": ("1 10 32", [5, 5, 5]),
        "fifo-8": ("0 0 8", [24]),
    }
    results = {}

    def steps(wait):
        with ws_run.section(1, f"receive events {dut_rx['pad']}"):
            for name, (args, bursts) in conditions.items():
                data = bytes((i * 29 + 3) & 0xFF for i in range(sum(bursts)))
                with ws_run.section(2, name, expect=[uart(dut_rx["pad"], probe_baud, data=data.hex())]):
                    _ask(dut, wait, f"uevt {args}", rb"UEVT 1")
                    at = 0
                    for n in bursts:
                        io.write(data[at:at + n])
                        at += n
                        wait(0.03)
                    m = _ask(dut, wait, "uevq", rb"UEVQ (\d+) (\d+) (\d+) ([0-9A-F]+) ([0-9,]*)")
                    ws_run.reply(m.group(0).decode())
                    seen = [int(x) for x in m.group(5).decode().split(",") if x]
                    results[name] = dict(calls=int(m.group(1)), read=int(m.group(2)), left=int(m.group(3)),
                                         hash=m.group(4).decode(), seen=seen, want=f"{_fnv(data):X}")

    try:
        ws_capture.stream(ws_run, oep_host, [dut_rx], rate=SLOW_RATE, steps=steps)
    finally:
        from oep_client import core
        core.plan_release(host, [used])
        _ask_now(dut, "uevt off", rb"UEVT \d")
    record_property("events", json.dumps(results))
    wrong = []
    for name, (_, bursts) in conditions.items():
        r = results.get(name)
        if r is None:
            wrong.append(f"{name}: not run")
            continue
        if r["hash"] != r["want"] or r["read"] + r["left"] != sum(bursts):
            wrong.append(f"{name}: {r['read']} read + {r['left']} left, hash {r['hash']} (want {sum(bursts)}, {r['want']})")
        elif name.startswith("timeout") and (r["calls"] != len(bursts) or r["seen"] != bursts or r["left"]):
            wrong.append(f"{name}: calls {r['calls']} saw {r['seen']} left {r['left']}, want {bursts}")
        elif name.startswith("fifo") and (not r["calls"] or min(r["seen"]) < 8 or r["left"] >= 8):
            wrong.append(f"{name}: calls {r['calls']} saw {r['seen']} left {r['left']}, want each >= 8, < 8 left")
    assert not wrong, " | ".join(wrong)


SOFT_CONDITIONS = [(9600, 0), (57600, 0), (115200, 0), (9600, 1), (9600, 2), (9600, 3), (9600, 4)]
# SoftwareSerial receives in a pin interrupt and times bits by SysTick: how fast a part follows depends on its
# interrupt latency, not only its clock (V003 and V006 both at 24 MHz: V003 takes 115200, V006 7 bytes of 16 there,
# 2026-11-17). Like an I2C clock above 400 kHz (the user, 2026-11-15: what can be set but may not work is not an
# error), the receive above SOFT_PROMISED_BAUD is recorded, not required; what the DUT sends is judged at every speed.
SOFT_PROMISED_BAUD = 9600


def test_uart_software_serial(dut, ws_run, request, record_property):
    """SoftwareSerial on the first two plain lines (RX, TX), against the probe's UART: 8N1 at 9600, 57600 and 115200,
    and 8E1, 8O1, 8N2, 7E1 at 9600. Each condition: the DUT sends 16 bytes (decoded on its TX at the nominal baud
    within 3 %, the format, no error) and receives 16 the probe sends (decoded on its RX; the DUT's count and FNV-1a,
    no overflow); the other line stays idle."""
    from oep_client import core, fixture
    from wireskein.runlog import ends, level, starts, uart
    target, oep_host = _oep_only(request)
    dut_tx, dut_rx = _serial_lines(oep_host, target)
    plain = _plain_lines(oep_host, target, dut_tx, dut_rx)
    if len(plain) < 2:
        pytest.skip("SoftwareSerial needs two plain <port>@<pad> lines")
    ss_rx, ss_tx = plain[0], plain[1]
    record_property("lines", f"rx {ss_rx['pad']} tx {ss_tx['pad']}")
    io, used = _plan_uart(oep_host, rx=ss_tx, tx=ss_rx)
    assert io is not None, "no free oep.fixture.uart takes the two lines"
    host = oep_host.host
    sent = _pattern(COUNT)
    got = {}

    def steps(wait):
        idle = {ss_tx["pad"]: 1, ss_rx["pad"]: 1}
        with ws_run.section(1, f"SoftwareSerial rx {ss_rx['pad']} tx {ss_tx['pad']}"):
            for baud, k in SOFT_CONDITIONS:
                bits, parity, stop = FORMATS[k]
                name = f"{baud} {bits}{parity[0].upper()}{stop}"
                mask = (1 << bits) - 1
                out = bytes(b & mask for b in sent)
                back = bytes(b & mask for b in reversed(sent))
                common = dict(bits=bits, parity=parity, stop=stop)
                # Begun before the section: until the first begin() the TX pad is no output yet.
                m = _ask(dut, wait, f"ssbegin {ss_rx['pin']} {ss_tx['pin']} {baud} {k}", rb"SSBEGIN (\d)")
                assert m.group(1) == b"1", f"{name}: SoftwareSerial is not listening"
                probe_baud = io.configure(baud, fixture.FixtureUart.format_byte(bits, parity[0].upper(), stop))
                wait(0.02)
                with ws_run.section(2, name, expect=[starts(idle), ends(idle)]):
                    ws_run.command(f"ssbegin {baud} {k}")
                    with ws_run.section(3, "dut sends", expect=[uart(ss_tx["pad"], baud, data=out.hex(), **common),
                                                                level(ss_rx["pad"], 1)]):
                        ws_run.command(f"sssend {COUNT}")
                        _ask(dut, wait, f"sssend {COUNT}", rb"SSSEND \d+", timeout=5)
                        wait(0.02)
                        io.read(4096)
                    with ws_run.section(3, "dut receives", expect=[uart(ss_rx["pad"], probe_baud, data=back.hex(),
                                                                        **common), level(ss_tx["pad"], 1)]):
                        ws_run.command(f"ssrecv {COUNT}")
                        dut.write(f"ssrecv {COUNT} {int(200 + 12000 * COUNT / baud)}\n")
                        _expect(dut, wait, rb"SSRECV READY")
                        io.write(back)
                        m = _expect(dut, wait, rb"SSRECV (\d+) ([0-9A-F]+) (\d)", timeout=5)
                        ws_run.reply(m.group(0).decode())
                        got[name] = (int(m.group(1)), m.group(2).decode(), m.group(3) == b"1")
                        wait(0.02)

    try:
        ws_capture.stream(ws_run, oep_host, [ss_rx, ss_tx], rate=SLOW_RATE, steps=steps)
    finally:
        core.plan_release(host, [used])
        _ask_now(dut, "ssend", rb"SSEND")
    record_property("received", json.dumps(got))
    wrong = []
    for baud, k in SOFT_CONDITIONS:
        bits, parity, stop = FORMATS[k]
        name = f"{baud} {bits}{parity[0].upper()}{stop}"
        back = bytes(b & ((1 << bits) - 1) for b in reversed(sent))
        if got.get(name) != (COUNT, f"{_fnv(back):X}", False) and baud <= SOFT_PROMISED_BAUD:
            wrong.append(f"{name}: {got.get(name)}")
    record_property("received_beyond", json.dumps({n: v for n, v in got.items()
                                                   if int(n.split()[0]) > SOFT_PROMISED_BAUD}))
    assert not wrong, "the DUT received: " + " | ".join(wrong)
