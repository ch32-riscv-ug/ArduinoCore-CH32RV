"""uart.external-round-trip: default USART route, 115200 8N1, binary 64-byte payload; then stdio on
the same port: puts() reaches Serial once it is begun, ch32rv_set_stdout(nullptr) discards, and
ch32rv_set_stdout(&Serial) brings it back.

uart.output-only (OEP ports with <port>.rx@<pad>): the formats' conditions with no UART on the probe, the DUT's TX
only, decoded the same way - the baseline. uart.formats (OEP ports with <port>.rx@<pad> and <port>.tx@<pad> labelled): 9600, 115200 and 1 Mbaud at 8N1, and
8E1, 8O1, 8N2, 7E1, 7O1 at 115200, both ways, in one capture of the two lines with a WireSkein section per
condition (both lines idle at its start and end): the DUT sends 16 bytes (decoded on its TX: the bytes, the format, the bit time F_CPU / BRR within 3 %,
no framing or parity error) and receives 16 the probe's UART sends (decoded on its RX the same way, and the DUT's
count and FNV-1a hash of what it read); the other line stays idle meanwhile.

uart.receive-errors (the same labels): the probe's UART set off from the DUT's format on purpose, and what
Serial.onReceiveError() reports compared with the kind made (test_uart_receive_errors)."""
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
    (the bytes, the format, the bit time F_CPU / BRR within 3 %, no framing or parity error), idle at the section's
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
    f_cpu = int(target["f_cpu"])
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
                    _expect(dut, wait, rb"UCFG 1")
                    wait(0.02)
                    with ws_run.section(3, "dut sends", expect=[uart(dut_tx["pad"], f_cpu / round(f_cpu / baud),
                                                                     data=out.hex(), bits=bits, parity=parity,
                                                                     stop=stop)]):
                        ws_run.command(f"usend {COUNT}")
                        dut.write(f"usend {COUNT}\n")
                        _expect(dut, wait, rb"USEND \d+")
                        wait(0.02 + 12 * COUNT / baud)

    try:
        ws_capture.stream(ws_run, oep_host, [dut_tx], rate=5_000_000, steps=steps)
    finally:
        dut.write("ucfg 115200 0\n")
        dut.expect(rb"UCFG \d\r?\n", timeout=5)


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
    f_cpu = int(target["f_cpu"])
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
                dut_baud = f_cpu / round(f_cpu / baud)         # what BRR makes of it
                out = bytes(b & mask for b in sent)
                back = bytes(b & mask for b in reversed(sent))
                idle = {dut_tx["pad"]: 1, dut_rx["pad"]: 1}
                with ws_run.section(2, name, expect=[starts(idle), ends(idle)]):
                    ws_run.command(f"ucfg {baud} {k}")
                    assert ask(wait, f"ucfg {baud} {k}", rb"UCFG (\d)").group(1) == b"1", f"{name}: Serial.begin refused"
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
        ws_capture.stream(ws_run, oep_host, [dut_tx, dut_rx], rate=5_000_000, steps=steps)
    finally:
        core.plan_release(host, [used])
        dut.write("ucfg 115200 0\n")
        dut.expect(rb"UCFG \d\r?\n", timeout=5)
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
                    assert _expect_after(dut, wait, f"ucfg {baud} {k}", rb"UCFG (\d)").group(1) == b"1"
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
        ws_capture.stream(ws_run, oep_host, [dut_tx, dut_rx], rate=5_000_000, steps=steps)
    finally:
        core.plan_release(host, [used])
        dut.write("ucfg 115200 0\n")
        dut.expect(rb"UCFG \d\r?\n", timeout=5)
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
