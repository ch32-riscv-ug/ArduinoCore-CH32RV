"""spi.modes (case spi): the hardware SPI on its default pins, CS the bench's spi_cs pad; softspi.modes (case
softspi): a SoftSPI on the bench's soft_spi pads ({sck, miso, mosi, cs}). Either is the controller of the OEP probe's
spi-target (tests/sketch_support/spi/spi.ino), every pad in the gpio table. In each of the four modes and
both bit orders the sketch sends a few bytes with transfer() and a word with transfer16(), CS low around them.

What must hold, judged on the wire (the probe's logic capture of CS, SCK and MOSI): 40 clocks while CS is low and the
bytes, then the word high byte first under MSBFIRST and low byte first under LSBFIRST (as the hardware SPI and AVR send
it); and the sketch reads back what the target was armed with, the word assembled the same way. The P4's spi-target
now and then counts 41 bits of a clean 40-clock transfer (2026-11-03, hardware SPI at ~750 kHz, the capture at 20 MS/s
shows no glitch); then its MISO is shifted too, and that is recorded as the target's, not failed."""
import json
import pathlib
import secrets

from oep_client import capture, core
from oep_client.fixture import SpiTarget


def settings(test_file):
    return json.loads(pathlib.Path(test_file).with_name("case.json").read_text())


def ask(dut, line, reply, timeout=10):
    dut.write(line + "\n")
    return dut.expect(reply + rb"\r?\n", timeout=timeout)


def pin(name):
    """The sketch's pin number of a pad name (the variants' CH32RV_PIN(port, bit))."""
    return ((ord(name[1]) - ord("A") + 2) << 5) | int(name[2:])


# (rate, samples) of the capture: the hardware SPI (~750 kHz) at ~13 samples a half period over 400 us; SoftSPI
# (a few us a half period) over 4 ms. Bigger windows at these rates overrun the P4's capture store.
WINDOW = {"hardware": (20_000_000, 8_000), "software": (5_000_000, 20_000)}


def wire_bits(cs, sck, mosi, mode):
    """MOSI sampled at the edges the mode samples on, while CS is low: what the DUT put on the wire."""
    sample_rising = mode in (0, 3)
    out = []
    for i in range(1, len(cs)):
        if cs[i] or cs[i - 1]:
            continue
        if sck[i] != sck[i - 1] and (sck[i] == 1) == sample_rising:
            out.append(mosi[i])
    return out


def to_bytes(bits, lsb):
    out = bytearray()
    for k in range(0, len(bits) - len(bits) % 8, 8):
        chunk = bits[k:k + 8]
        out.append(sum(b << (j if lsb else 7 - j) for j, b in enumerate(chunk)))
    return bytes(out)


def pad(number):
    return f"P{'ABCDEF'[(number >> 5) - 2]}{number & 31}"


def test_spi_contract(dut, oep_host, record_property):
    case = settings(__file__)
    nonce = secrets.token_hex(4)
    m = ask(dut, f"ready {nonce}", rb"READY " + nonce.encode() + rb" (\S+)", timeout=15)
    assert m.group(1).decode() == case["build_id"], "the DUT runs another build"
    if "soft_spi" in case:
        pads = case["soft_spi"]
        assert ask(dut, f"pins {pin(pads['sck'])} {pin(pads['miso'])} {pin(pads['mosi'])} {pin(pads['cs'])}",
                   rb"PINS (\d)").group(1) == b"1", "SoftSPI refused the pins"
        record_property("bus", "SoftSPI")
    else:
        m = ask(dut, f"hw {pin(case['spi_cs'])}", rb"HW (\d+) (\d+) (\d+)")
        pads = dict(sck=pad(int(m.group(1))), miso=pad(int(m.group(2))), mosi=pad(int(m.group(3))), cs=case["spi_cs"])
        record_property("bus", "SPI")
    channels = {p["pad"]: p["channel"] for p in case["target"]["gpio"]}
    missing = [pads[r] for r in pads if pads[r] not in channels]
    assert not missing, f"the bench's gpio table has no channel for {missing}"
    ch = {role: channels[pads[role]] for role in ("sck", "miso", "mosi", "cs")}
    record_property("pins", json.dumps({r: f"{pads[r]} ch{c}" for r, c in ch.items()}))

    host = oep_host.host
    target = SpiTarget(host)
    cap = capture.LogicCapture(host)
    core.plan_apply(host, target.assignments(ch["sck"], ch["mosi"], ch["miso"], ch["cs"]) +
                    [(cap.fn, 0, ch["cs"]), (cap.fn, 1, ch["sck"]), (cap.fn, 2, ch["mosi"])])
    try:
        failures = []
        orders = [0, 1] if target.features & SpiTarget.FEATURE_LSB_FIRST else [0]
        record_property("orders", json.dumps(orders))
        for mode in range(4):
            for lsb in orders:
                out = secrets.token_bytes(3)
                word = int.from_bytes(secrets.token_bytes(2), "big")
                miso = secrets.token_bytes(5)
                target.configure(mode=mode, bit_order=lsb)
                target.arm(5, miso)
                rate, samples = WINDOW["software" if "soft_spi" in case else "hardware"]
                cap.configure(rate=rate, samples=samples, trigger=(capture.EDGE, 0, 1), pretrigger=100)
                cap.start()
                m = ask(dut, f"xfer {mode} {lsb} {word:04x} {out.hex()}", rb"XFER (\S+) ([0-9a-f]{4})")
                got_bytes, got_word = bytes.fromhex(m.group(1).decode()), int(m.group(2), 16)
                pending, bits, mosi = target.read_rx()
                segs = cap.wait(timeout=5.0)
                data = cap.read_segment(segs[0])
                cs_b, sck_b, mosi_b = (cap.channel(data, k, segs[0].samples) for k in range(3))
                seen = wire_bits(cs_b, sck_b, mosi_b, mode)
                # The shortest SCK level while CS is low, in samples: a glitch is far shorter than a half period.
                runs, last, n_run = [], None, 0
                for i in range(len(cs_b)):
                    if cs_b[i]:
                        continue
                    if sck_b[i] == last:
                        n_run += 1
                    else:
                        if last is not None:
                            runs.append(n_run)
                        last, n_run = sck_b[i], 1
                low = [i for i in range(len(cs_b)) if not cs_b[i]]
                edges = sum(1 for i in low[1:] if sck_b[i] != sck_b[i - 1])
                at_fall = sck_b[low[0]] if low else -1
                record_property(f"mode_{mode}_{'LSB' if lsb else 'MSB'}_sck",
                                f"shortest {min(runs[1:]) if len(runs) > 1 else -1}, {edges} edges, at CS fall {at_fall}, idle {mode >> 1}")
                wire_word = word.to_bytes(2, "little" if lsb else "big")
                want_word = int.from_bytes(miso[3:5], "little" if lsb else "big")
                label = f"mode {mode} {'LSB' if lsb else 'MSB'}FIRST"
                problems = []        # the DUT's
                probe = []           # the probe's spi-target's
                if len(seen) != 40 or to_bytes(seen, lsb) != out + wire_word:
                    problems.append(f"on the wire {len(seen)} bits, {to_bytes(seen, lsb).hex()}, not 40, {(out + wire_word).hex()}")
                if bits != 40 or mosi != out + wire_word:
                    probe.append(f"the target clocked {bits} bits, received {mosi.hex()}")
                read_wrong = []
                if got_bytes != miso[:3]:
                    read_wrong.append(f"read {got_bytes.hex()}, the target was armed with {miso[:3].hex()}")
                if got_word != want_word:
                    read_wrong.append(f"transfer16() read {got_word:04x}, not {want_word:04x}")
                # A target that counted otherwise shifted its MISO too: what the DUT read then is the probe's doing.
                (probe if bits != 40 else problems).extend(read_wrong)
                if probe:
                    record_property(label.replace(" ", "_") + "_target", "; ".join(probe))
                record_property(label.replace(" ", "_"), "; ".join(problems) or "ok")
                if problems:
                    failures.append(f"{label}: " + "; ".join(problems))
        assert not failures, " | ".join(failures)
    finally:
        core.plan_release(host, [target.fn, cap.fn])
