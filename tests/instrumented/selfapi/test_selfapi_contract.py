"""selfapi (case selfapi): the core's utility APIs with no wiring (tests/sketch_support/selfapi/selfapi.ino), beside
selfcheck, selfbus and selfsys - so they run on a WCH-LinkE's targets too.

selfapi.crc: ch32rv_crc32() / CH32RV.crc32() give CRC-32/MPEG-2 - the check value 0x0376E6E7 for "123456789",
0x199AC3CA for the bytes 1..128 (EVT CRC_Calculation's 32 words, 0x01020304 ... 0x7D7E7F80, as bytes), the same over
the 128 bytes in pieces of 1, 2, 3, 5, 7, ... (a running CRC through the block's seed word and the table), and what
this file computes for bytes 2..127 (an unaligned start, a tail of 2) - on the CRC block where the series has one
(the sketch says which), in software elsewhere.

selfapi.rng: 64 ch32rv_random() words and 8 ch32rv_fill_random() runs of 7 bytes: no word repeats, none is 0 or
0xFFFFFFFF, the 2048 bits are 1 within 50 % +- 5 % (4.5 sigma) and every bit position is 1 in 12-52 of the 64 words.
Statistical sanity only - a stuck or constant source fails; randomness itself is not measured. The sketch says
whether the hardware generator answered (V30x, V4x7, H41x) or the software mix (not cryptographic).

selfapi.millivolts: on up to four ADC1 pads (not kept off, not held) driven 1 and then 0 as an output,
analogReadMilliVolts()'s path (the reading against the Vrefint measured just before it) gives at least 80 % and at
most 110 % + 50 mV of the supply at 1 and at most 10 % of it at 0 - the supply being CH32RV.getVddMillivolts(),
or 3300 mV where the family has no reference in the data (M030). A wide band: the mV's accuracy is the manual
test's (an outside voltage source), not this one's. H41x's VIO18 pads (1.2 V on the board) are left out.

selfapi.priorities: two timers' update interrupts, the slow one's handler 300 us long, the fast one every 100 us:
with the slow one a preemption level less urgent (CH32RV_IRQ_PRIORITY(1)), at least 80 % of its handlers are
interrupted by the fast one (nesting); at the same level, and with the slow one more urgent, none is. At least 15
slow handlers ran in each 50 ms (one every 2 ms). n/a where the sketch found fewer than two free timers.

selfapi.cores-hsem / cores-ipc / cores-v3f-serial (CH32H41x; n/a elsewhere): with CPU Core = V5F one image holds
both cores' code and the V3F runs the sketch's functions (CH32RV.startV3F()). HSEM: what one core holds the other
can neither take nor release, released it can, the holder cannot take it twice, hsemTake(20 ms) gives up after 20-40
ms, and 20000 additions by each core under one channel lose none. IPC: 1000 round trips V5F -> V3F -> V5F all come
back, each handler in its own core's IPC interrupt (mcause 0x80000011 on core 0, 0x80000010 on core 1), at most 50 us
a round trip, and one message taken by polling. A USART begun on the V3F: its interrupt allocated to the V3F
(PFIC_IALLOCR), 64 bytes out at 115200 within selfcheck's band, 16 bytes in at 9600 from its own RX pad. With CPU
Core = V3F: the HSEM on one core, the IPC refusing (no other core).

A board whose record loses gpio-drive (CH32M030's motor board) skips millivolts and cores-v3f-serial."""
import json
import pathlib
import secrets

import pytest

POLY = 0x04C11DB7


def mpeg2(data, crc=0xFFFFFFFF):
    for b in data:
        crc ^= b << 24
        for _ in range(8):
            crc = ((crc << 1) ^ POLY) & 0xFFFFFFFF if crc & 0x80000000 else (crc << 1) & 0xFFFFFFFF
    return crc


def settings(test_file):
    return json.loads(pathlib.Path(test_file).with_name("case.json").read_text())


def pad(pin):
    return f"P{chr(ord('A') + (pin >> 5) - 2)}{pin & 31}"


def ask(dut, line, reply, timeout=10):
    dut.write(line + "\n")
    return dut.expect(reply + rb"\r?\n", timeout=timeout)


@pytest.fixture
def ready(dut):
    case = settings(__file__)
    nonce = secrets.token_hex(4)
    m = ask(dut, f"ready {nonce}", rb"READY " + nonce.encode() + rb" (\S+)", timeout=15)
    assert m.group(1).decode() == case["build_id"], "the DUT runs another build"
    return case["target"]


def test_crc(dut, ready, record_property):
    block, check, evt, pieces, unaligned = ask(
        dut, "crc", rb"CRC (\d) ([0-9a-f]{8}) ([0-9a-f]{8}) ([0-9a-f]{8}) ([0-9a-f]{8})").groups()
    got = dict(block=int(block), check=check.decode(), evt=evt.decode(), pieces=pieces.decode(),
               unaligned=unaligned.decode())
    record_property("crc", json.dumps(got))
    want = dict(check="0376e6e7", evt="199ac3ca", pieces="199ac3ca", unaligned=f"{mpeg2(bytes(range(2, 128))):08x}")
    assert mpeg2(b"123456789") == 0x0376E6E7 and mpeg2(bytes(range(1, 129))) == 0x199AC3CA
    wrong = [f"{k} {got[k]}, {v} expected" for k, v in want.items() if got[k] != v]
    assert not wrong, ("on the CRC block: " if got["block"] else "in software: ") + " | ".join(wrong)


def test_rng(dut, ready, record_property):
    words, fills, hw = [], [], None
    for _ in range(8):
        m = ask(dut, "rng", rb"RNG (\d)((?: [0-9a-f]{8}){8}) ([0-9a-f]{14})")
        hw = int(m.group(1))
        words += [int(w, 16) for w in m.group(2).split()]
        fills.append(m.group(3).decode())
    ones = sum(bin(w).count("1") for w in words)
    per_bit = [sum((w >> b) & 1 for w in words) for b in range(32)]
    record_property("rng", json.dumps(dict(hardware=bool(hw), ones=ones, bits=len(words) * 32, per_bit=per_bit,
                                           first=f"{words[0]:08x}", fills=fills[:2])))
    wrong = []
    if len(set(words)) != len(words):
        wrong.append(f"{len(words) - len(set(words))} repeated words")
    if any(w in (0, 0xFFFFFFFF) for w in words):
        wrong.append("a word of all 0 or all 1")
    if abs(ones / (len(words) * 32) - 0.5) > 0.05:
        wrong.append(f"{ones} of {len(words) * 32} bits are 1")
    bad = [b for b, n in enumerate(per_bit) if not 12 <= n <= 52]
    if bad:
        wrong.append(f"bit positions {bad} are 1 in {[per_bit[b] for b in bad]} of 64 words")
    if len(set(fills)) != len(fills):
        wrong.append("ch32rv_fill_random() repeated itself")
    assert not wrong, ("hardware generator: " if hw else "software mix: ") + " | ".join(wrong)


# CH32H41x's high-speed pads run on VIO18 (1.2 V on the evaluation board): driven 1 they read that, not the supply.
VIO18_PADS = {f"P{p}{b}" for p, bits in (("A", range(13, 16)), ("B", (0, 1, 10, 11, 12, 13, 14)),
                                        ("C", (0, 1, 2, 3, 6, 7, 8, 9, 10, 11, 12)),
                                        ("D", (0, 1, 2, 3, 4, 5, 6, 7, 9, 10, 11, 12, 13, 14, 15)),
                                        ("E", (0, 1, 7, 8, 9, 10, 11, 12, 13, 14, 15)),
                                        ("F", (0, 1, 2, 3, 4, 5, 11, 12, 13, 14))) for b in bits}


def test_millivolts(dut, ready, record_property):
    for d in ready.get("record", {}).get("defects", []):
        if "gpio-drive" in d.get("lose", []):
            pytest.skip(f"the board's pads are not driven: defect {d.get('id')}")
    off = ready.get("gpio_keep_off")
    if off is None:
        pytest.skip("ch32-device-data is not fetched: no list of the pads to keep off")
    lost = {f for d in ready.get("record", {}).get("defects", []) for f in d.get("lose", [])}
    h41x = ready.get("chip", "").startswith("CH32H41")
    pins = [int(p) for p in ask(dut, "adcpins", rb"ADCPINS([ \d]*)").group(1).split()]
    results, wrong, left = {}, [], []
    for pin in pins:
        if len(results) == 4:
            break
        if pad(pin) in set(off) or (h41x and pad(pin) in VIO18_PADS):
            continue
        m = ask(dut, f"mv {pin}", rb"MV -?\d+ (?:(HELD|NONE)|(\d+) (\d+) (\d+) (\d+))")
        if m.group(1):
            left.append(f"{pad(pin)} {m.group(1).decode().lower()}")
            continue
        channel, high, low, vdd = (int(x) for x in m.groups()[1:])
        supply = vdd or 3300
        results[pad(pin)] = dict(ch=channel, high_mv=high, low_mv=low, vdd_mv=vdd)
        if f"adc:{channel}" in lost:
            continue                    # a channel the board's record loses (X035's errata lot): recorded only
        if not 0.8 * supply <= high <= 1.1 * supply + 50 or low > 0.1 * supply:
            wrong.append(f"{pad(pin)}: {high} mV at 1, {low} mV at 0, the supply {supply} mV")
    record_property("millivolts", json.dumps(results))
    record_property("left", json.dumps(left))
    if not results:
        pytest.skip("no ADC pad is free" + (": " + ", ".join(left) if left else ""))
    assert not wrong, " | ".join(wrong)


def test_priorities(dut, ready, record_property):
    m = ask(dut, "prio", rb"PRIO (?:(NONE)|(\d+) (\d+) (\d+) (\d+) (\d+) (\d+) (\d+))", timeout=15)
    if m.group(1):
        pytest.skip("n/a: fewer than two free timers")
    levels, *counts = (int(x) for x in m.groups()[1:])
    names = ("slow less urgent", "same level", "slow more urgent")
    seen = {n: dict(runs=counts[2 * i], nested=counts[2 * i + 1]) for i, n in enumerate(names)}
    record_property("priorities", json.dumps(dict(levels=levels, **seen)))
    wrong = [f"{n}: {s['runs']} slow handlers in 50 ms" for n, s in seen.items() if s["runs"] < 15]
    first = seen[names[0]]
    if first["nested"] < 0.8 * first["runs"]:
        wrong.append(f"{names[0]}: {first['nested']} of {first['runs']} slow handlers interrupted (nesting)")
    wrong += [f"{n}: {seen[n]['nested']} slow handlers interrupted" for n in names[1:] if seen[n]["nested"]]
    assert not wrong, f"{levels} preemption levels: " + " | ".join(wrong)



# CH32H41x's two cores (selfapi.cores-*): the HSEM, the IPC and a USART used from the V3F, all from one image with
# CPU Core = V5F - the V3F runs functions of the same sketch (CH32RV.startV3F()), so one write puts both cores' code
# on the chip. With CPU Core = V3F the V5F stays asleep: hsem runs on one core and ipc must refuse.
def cores(dut):
    m = ask(dut, "cores", rb"CORES (V5F (\d+)|V3F|NONE)")
    mode = m.group(1).decode().split()[0]
    if mode == "NONE":
        pytest.skip("n/a: one core (CH32H41x only)")
    return mode, int(m.group(2) or 0)


def test_cores_hsem(dut, ready, record_property):
    mode, _ = cores(dut)
    if mode == "V3F":
        m = ask(dut, "hsem", rb"HSEM V3F (\d) (\d) (\d) (\d) (\d) ([0-9a-f]{8})")
        got = [int(x) for x in m.groups()[:5]]
        record_property("hsem", json.dumps(dict(mode=mode, steps=got, raw=m.group(6).decode())))
        # take, take again (this core holds it), release, release again (not held), take(20 ms) of a free one
        assert got == [1, 0, 1, 0, 1], f"one core: take/again/release/again/take gave {got}, want [1, 0, 1, 0, 1]"
        return
    m = ask(dut, "hsem", rb"HSEM V5F (?:(BUSY)|" + rb" ".join([rb"(\d+)"] * 13) + rb" ([0-9a-f]{8}))", timeout=15)
    assert not m.group(1), "the V3F was busy: startV3F() refused"
    a, b, c, d, e, f, g, h, waited, j, i, count, expected = (int(x) for x in m.groups()[1:14])
    record_property("hsem", json.dumps(dict(mode=mode, steps=[a, b, c, d, e, f, g, h, j, i], waited_ms=waited,
                                            count=count, expected=expected, raw=m.group(15).decode())))
    want = dict(v5f_take=(a, 1), v5f_again=(b, 0), v3f_take_while_held=(c, 0), v3f_release_not_its=(d, 0),
                v5f_release=(e, 1), v3f_take_after=(f, 1), v5f_try_while_v3f=(g, 0), v5f_take_20ms=(h, 0),
                v3f_release=(j, 1), v5f_take_after=(i, 1))
    wrong = [f"{k} {v[0]}, want {v[1]}" for k, v in want.items() if v[0] != v[1]]
    # take(20 ms) must have waited its time: at least 20 ms (millis() granularity 1 ms), and not run on - 40 ms is
    # twice the limit, far above the 70-280 ns a try costs (docs/h417-multicore.ja.md).
    if not 20 <= waited <= 40:
        wrong.append(f"take(20 ms) of a held channel returned after {waited} ms")
    # Both cores add 1 to a plain shared word under one channel, 20000 times each: exclusion holds only if none is
    # lost (without it 25 % were, docs/h417-multicore.ja.md).
    if count != expected:
        wrong.append(f"the count under the semaphore is {count}, {expected} expected")
    assert not wrong, " | ".join(wrong)


def test_cores_ipc(dut, ready, record_property):
    mode, _ = cores(dut)
    n = 1000
    if mode == "V3F":
        m = ask(dut, f"ipc {n}", rb"IPC V3F (\d) (\d) (\d)")
        got = [int(x) for x in m.groups()]
        record_property("ipc", json.dumps(dict(mode=mode, results=got)))
        assert got == [0, 0, 0], f"CPU Core = V3F (no other core): send/onReceive/receive gave {got}, want 0 0 0"
        return
    m = ask(dut, f"ipc {n}", rb"IPC V5F (?:(BUSY)|(\d+) (\d+) (\d+) ([0-9a-f]{8}) (\d+) ([0-9a-f]{8}) (\d+) (\d+) "
                             rb"([0-9a-f]{8}))", timeout=20)
    assert not m.group(1), "the V3F was busy: startV3F() refused"
    asked, ok, us = (int(x) for x in m.groups()[1:4])
    v3f_cause, v3f_core = int(m.group(5), 16), int(m.group(6))
    v5f_cause, v5f_core = int(m.group(7), 16), int(m.group(8))
    runs, polled = int(m.group(9)), int(m.group(10), 16)
    per = us / ok if ok else None
    record_property("ipc", json.dumps(dict(mode=mode, asked=asked, answered=ok, us=us, us_per_round_trip=per,
                                           v3f_mcause=hex(v3f_cause), v5f_mcause=hex(v5f_cause),
                                           v3f_runs=runs, polled=hex(polled))))
    wrong = []
    if ok != asked or runs != asked:
        wrong.append(f"{ok} of {asked} round trips answered, the V3F's handler ran {runs} times")
    # The handlers run in each core's own IPC interrupt: mcause is an interrupt (bit 31) with IPC_CH1's number (17)
    # on the V3F (core 0) and IPC_CH0's (16) on the V5F (core 1) - RX_CID routes them, RM 4.4.
    if (v3f_cause, v3f_core) != (0x80000011, 0):
        wrong.append(f"the V3F's handler: mcause {v3f_cause:#x} on core {v3f_core}, want 0x80000011 on core 0")
    if (v5f_cause, v5f_core) != (0x80000010, 1):
        wrong.append(f"the V5F's handler: mcause {v5f_cause:#x} on core {v5f_core}, want 0x80000010 on core 1")
    if polled != 0x5A5A1234:
        wrong.append(f"ipcReceive() polled {polled:#x}, 0x5a5a1234 sent")
    # A round trip is two interrupt entries, a V3F handler at 100 MHz and the V5F's wait loop (2 us steps): a few
    # microseconds. 50 us leaves a factor of ten for the V5F's ISR and its pauses; above it, something waits for a
    # timeout or a poll instead of the interrupt.
    if per is not None and per > 50:
        wrong.append(f"{per:.1f} us a round trip ({us} us for {ok})")
    assert not wrong, " | ".join(wrong)


def test_cores_v3f_serial(dut, ready, record_property):
    mode, count = cores(dut)
    if mode != "V5F":
        pytest.skip("n/a: CPU Core = V3F runs the whole sketch on the V3F (the other cases cover its Serial)")
    for d in ready.get("record", {}).get("defects", []):
        if "gpio-drive" in d.get("lose", []):
            pytest.skip(f"the board's pads are not driven: defect {d.get('id')}")
    off = ready.get("gpio_keep_off")
    if off is None:
        pytest.skip("ch32-device-data is not fetched: no list of the pads to keep off")
    off = set(off)
    results, wrong, left = {}, [], []
    for k in range(count):
        number, pin = (int(x) for x in ask(dut, f"v3fport {k}", rb"V3FPORT (\d+) (-?\d+)").groups())
        if not number:
            left.append(f"port {k} (closed)")
            continue
        if pin >= 0 and pad(pin) in off:
            left.append(f"USART{number} ({pad(pin)}: kept off)")
            continue
        m = ask(dut, f"v3fser {k}", rb"V3FSER (\d+) (?:(CLOSED|BUSY)|(\d+) (\d+) (\d+) (?:(HELD)|(\d+) (\d+) "
                                    rb"([0-9a-f]+)))", timeout=15)
        assert m.group(2) != b"BUSY", "the V3F did not start or return"
        if m.group(2):
            left.append(f"USART{number} (closed)")
            continue
        sent, took, alloc = (int(x) for x in m.groups()[2:5])
        ideal = 64 * 10 * 1e6 / 115200
        entry = dict(sent=sent, us=took, ialloc=alloc)
        # selfcheck's transmit band: 64 bytes at 115200 within 4 % and 200 us of the line's time - the TXE
        # interrupt, on the V3F, keeps the line busy.
        if sent != 64 or not 0.96 * ideal <= took <= 1.04 * ideal + 200:
            wrong.append(f"USART{number}: sent {sent} of 64 in {took} us, {ideal:.0f} at 115200")
        # PFIC_IALLOCR: enabling a shared interrupt gives it to the enabling core - 0 is the V3F (RM 4.7.5.45).
        if alloc != 0:
            wrong.append(f"USART{number}: its interrupt is allocated to core {alloc}, the V3F (0) began it")
        if m.group(6):
            entry["rx"] = "held"
        else:
            received, matching, errors = int(m.group(7)), int(m.group(8)), int(m.group(9), 16)
            entry.update(received=received, matching=matching, errors=errors)
            # selfcheck's receive: all 16 bytes, in order, no error - the RXNE interrupt taken on the V3F.
            if received != 16 or matching != 16 or errors:
                wrong.append(f"USART{number}: received {received}, {matching} in order of 16, errors {errors:#x}")
        results[f"USART{number}"] = entry
    record_property("ports", json.dumps(results))
    if left:
        record_property("left", ", ".join(left))
    assert results, "no port could be used: " + ", ".join(left)
    assert any("received" in r for r in results.values()), "no port's RX pad could be driven: " + json.dumps(results)
    assert not wrong, " | ".join(wrong)
