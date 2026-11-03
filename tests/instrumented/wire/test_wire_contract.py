"""wire.repeated-start: Wire as the controller of the OEP probe's i2c-target on the default pins at 100 kHz
(tests/sketch_support/wire/wire.ino), with the probe's logic capture listening on the same two channels. The sketch
writes 0x10 with endTransmission(false), reads n bytes with requestFrom(addr, n, stop) and writes 0x20 with
endTransmission(true), back to back, for n = 1, 2 and 3 (the driver's three read paths).

Arduino's sendStop: false keeps the bus, so the next transfer starts with a repeated START. On the wire that is
S W Sr R Sr W P with stop 0, and S W Sr R P S W P with stop 1. The pads come from the bench's gpio table.

wire.softwire: the same on a SoftWire (case softwire), on the pads the bench names as soft_wire
({sda, scl}), which need not be a hardware route. Wire itself is the board's: a hardware I2C, or a
SoftWire when the fqbn sets that I2C to Software (i2c1=software), which case wire then covers."""
import json
import pathlib
import secrets

from oep_client import capture, core
from oep_client.fixture import I2cTarget

ADDRESS = 0x42
RATE = 2_000_000          # 20 samples a bit at 100 kHz
SAMPLES = 20_000          # 10 ms; the sequence takes under 2 ms
PRETRIGGER = 200


def settings(test_file):
    return json.loads(pathlib.Path(test_file).with_name("case.json").read_text())


def ask(dut, line, reply, timeout=10):
    dut.write(line + "\n")
    return dut.expect(reply + rb"\r?\n", timeout=timeout)


def pad(pin):
    return f"P{'ABCDEF'[(pin >> 5) - 2]}{pin & 31}"


def pin(name):
    """The sketch's pin number of a pad name (the variants' CH32RV_PIN(port, bit))."""
    return ((ord(name[1]) - ord("A") + 2) << 5) | int(name[2:])


def conditions(sda, scl):
    """START (SDA falls while SCL stays high) and STOP (SDA rises while SCL stays high), in order."""
    out = []
    for i in range(1, len(sda)):
        if scl[i - 1] and scl[i] and sda[i] != sda[i - 1]:
            out.append("P" if sda[i] else "S")
    return out


def bits(sda, scl):
    """What went on the wire for a failure message: S / P and the bits clocked in between."""
    out = []
    for i in range(1, len(sda)):
        if scl[i - 1] and scl[i] and sda[i] != sda[i - 1]:
            out.append(" P " if sda[i] else " S ")
        elif scl[i] and not scl[i - 1]:
            out.append(str(sda[i]))
    return "".join(out).strip()


def test_wire_contract(dut, oep_host, record_property):
    case = settings(__file__)
    nonce = secrets.token_hex(4)
    m = ask(dut, f"ready {nonce}", rb"READY " + nonce.encode() + rb" (\S+) (\d+) (\d+)", timeout=15)
    assert m.group(1).decode() == case["build_id"], "the DUT runs another build"
    sda_pad, scl_pad = pad(int(m.group(2))), pad(int(m.group(3)))
    if "soft_wire" in case:
        sda_pad, scl_pad = case["soft_wire"]["sda"], case["soft_wire"]["scl"]
        m = ask(dut, f"soft {pin(sda_pad)} {pin(scl_pad)}", rb"SOFT (\d)")
        assert m.group(1) == b"1", f"SoftWire refused SDA {sda_pad} / SCL {scl_pad}"
        record_property("bus", "SoftWire")
    channels = {p["pad"]: p["channel"] for p in case["target"].get("gpio", [])}
    assert sda_pad in channels and scl_pad in channels, f"the bench's gpio table has no channel for {sda_pad} / {scl_pad}"
    sda, scl = channels[sda_pad], channels[scl_pad]
    record_property("pins", f"SDA {sda_pad} ch{sda}, SCL {scl_pad} ch{scl}")

    host = oep_host.host
    target = I2cTarget(host)
    cap = capture.LogicCapture(host)
    core.plan_apply(host, target.assignments(sda, scl) + [(cap.fn, 0, sda), (cap.fn, 1, scl)])
    try:
        record_property("probe_pullups", target.internal_pullups)
        target.configure(ADDRESS)
        results = {}
        for n in (1, 2, 3):
            for stop in (0, 1):
                reply = secrets.token_bytes(n)
                target.preload_tx(reply)
                cfg = cap.configure(rate=RATE, samples=SAMPLES, trigger=(capture.EDGE, 0, 1), pretrigger=PRETRIGGER)
                cap.start()
                m = ask(dut, f"seq {ADDRESS} {n} {stop}", rb"SEQ (\d+) (\d+) (\S+) (\d+)")
                segs = cap.wait(timeout=5.0)
                data = cap.read_segment(segs[0])
                first, got, hexed, last = int(m.group(1)), int(m.group(2)), m.group(3).decode(), int(m.group(4))
                frames = []
                while True:
                    pending, frame = target.read_rx()
                    if frame:
                        frames.append(frame)
                    if not pending:
                        break
                sda_bits, scl_bits = cap.channel(data, 0, segs[0].samples), cap.channel(data, 1, segs[0].samples)
                seen = conditions(sda_bits, scl_bits)
                results[f"n{n}_stop{stop}"] = "".join(seen)
                record_property("conditions", json.dumps(results))
                assert (first, last) == (0, 0), \
                    f"n {n} stop {stop}: endTransmission gave {first} / {last}; {bits(sda_bits, scl_bits) or 'nothing'} on the wire, target {target.status()}"
                assert got == n and hexed.lower() == reply.hex(), f"n {n} stop {stop}: read {got} bytes {hexed}, the target sent {reply.hex()}"
                # The bytes, not the framing: a probe may join the two writes across the read (the
                # conditions on the wire are what this contract judges).
                record_property(f"frames_n{n}_stop{stop}", repr(frames))
                assert b"".join(frames) == b"\x10\x20", f"n {n} stop {stop}: the target received {frames}"
                want = ["S", "S", "P", "S", "P"] if stop else ["S", "S", "S", "P"]
                assert seen == want, f"n {n} stop {stop}: {''.join(seen)} on the wire, not {''.join(want)} (rate {float(cfg.rate):.0f})"
    finally:
        core.plan_release(host, [target.fn, cap.fn])
