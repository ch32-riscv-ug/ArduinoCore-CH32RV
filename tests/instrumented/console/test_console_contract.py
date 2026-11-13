"""console-rtt / console-dmdata / console-sdi: a debug console as the sketch's only one
(tests/sketch_support/console/console.ino), read through the probe's monitor with that source (the case's
port_config, run_hardware.py).

- console-rtt and console-dmdata, both ways: the sketch answers a nonce with its build id; 20 lines of random text
  come back as sent (echo, up to 80 characters); a burst of 200 numbered lines with a hash each arrives whole and
  in order.
- console-sdi, send only (docs in libraries/SerialSDI): a line every 100 ms with the build id and a count; 20 of them
  arrive with no count skipped. Which probes carry it is ch32rv's capabilities' answer (`monitor sdi`: a WCH-LinkE and
  an OEP probe do, a WCH-Link(CH549) does not); harness/place.py skips the case where it is not supported."""
import json
import pathlib
import secrets
import string

import pytest


def settings(test_file):
    return json.loads(pathlib.Path(test_file).with_name("case.json").read_text())


def fnv(text):
    h = 2166136261
    for b in text.encode():
        h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    return h


def ready(dut, case, tries=10):
    for _ in range(tries):
        nonce = secrets.token_hex(4)
        dut.write(f"ready {nonce}\n")
        try:
            m = dut.expect(rb"READY " + nonce.encode() + rb" (\S+)\r?\n", timeout=2)
            assert m.group(1).decode() == case["build_id"], "the DUT runs another build"
            return
        except Exception as e:                 # the first lines may go before the monitor attaches
            last = e
    raise AssertionError(f"no READY: {last}")


def test_console_two_way(dut, record_property):
    case = settings(__file__)
    if case.get("case") == "console-sdi":
        pytest.skip("SerialSDI only sends")
    ready(dut, case)
    alphabet = string.ascii_letters + string.digits + " .,:-_"
    for n in range(20):
        text = "".join(secrets.choice(alphabet) for _ in range(1 + secrets.randbelow(80))).strip() or "x"
        dut.write(f"echo {text}\n")
        dut.expect_exact(f"ECHO {text}", timeout=5)
    dut.write("burst 200\n")
    for i in range(200):
        m = dut.expect(rb"BURST (\d+) ([0-9A-F]+)\r?\n", timeout=10)
        assert int(m.group(1)) == i, f"burst line {m.group(1).decode()} where {i} was due"
        assert int(m.group(2), 16) == fnv(f"line {i}"), f"burst line {i}: the hash differs"
    dut.expect_exact("BURST DONE", timeout=10)
    record_property("echoed", 20)
    record_property("burst", 200)


def test_console_sdi(dut, record_property):
    case = settings(__file__)
    if case.get("case") != "console-sdi":
        pytest.skip("the two-way consoles are test_console_two_way's")
    counts = []
    while len(counts) < 21:
        m = dut.expect(rb"SDI (\S+) (\d+)\r?\n", timeout=5)
        assert m.group(1).decode() == case["build_id"], "the DUT runs another build"
        counts.append(int(m.group(2)))
    steps = [b - a for a, b in zip(counts[1:], counts[2:])]       # the first line may be cut short
    record_property("counts", json.dumps(counts))
    assert all(s == 1 for s in steps), f"SDI counts skipped: {counts}"
