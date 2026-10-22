"""synckit.queue / synckit.notify / synckit.ring / synckit.critical: CH32RVSyncKit's IsrSafe
parts under a real interrupt (tests/sketch_support/synckit/synckit.ino). A timer update
interrupt at 10 kHz feeds a Queue, a Notify counter and a Ring and bumps a 64-bit value
whose halves always match, while loop() drains and checks for 3 s. No wiring.

What must hold, on every series (CH32V003 included, which has no atomic instructions):
nothing is lost or duplicated that the parts did not report as refused, the 64-bit value
read inside a CriticalSection is never torn, and the critical section stays short. The
unguarded read of the same value is recorded as the negative control; it may or may not
catch a torn read in a given run."""
import json
import pathlib
import secrets


def settings(test_file):
    return json.loads(pathlib.Path(test_file).with_name("case.json").read_text())


def ask(dut, line, reply, timeout=10):
    dut.write(line + "\n")
    return dut.expect(reply + rb"\r?\n", timeout=timeout)


FIELDS = ("isr_calls", "sent", "queue_dropped", "received", "unexplained_gaps", "dups", "notified",
          "taken", "ring_written", "ring_dropped", "ring_read", "ring_breaks", "torn_guarded",
          "torn_unguarded", "reads", "max_cs_us")


def test_synckit_contract(dut, record_property):
    nonce = secrets.token_hex(4)
    m = ask(dut, f"ready {nonce}", rb"READY " + nonce.encode() + rb" (\S+) (\d+)", timeout=15)
    assert m.group(1).decode() == settings(__file__)["build_id"], "the DUT runs another build"
    assert m.group(2) != b"0", "no timer with an update interrupt could be leased"
    record_property("timer", int(m.group(2)))

    m = ask(dut, "run 3000", rb"RUN" + rb" (\d+)" * len(FIELDS), timeout=15)
    r = dict(zip(FIELDS, (int(g) for g in m.groups())))
    for k, v in r.items():
        record_property(k, v)

    # synckit.queue: every number the ISR used up was received or reported dropped, in order
    assert 25000 <= r["isr_calls"] <= 35000, "the 10 kHz interrupt did not run for ~3 s"
    assert r["sent"] == r["isr_calls"]
    assert r["received"] + r["queue_dropped"] == r["sent"]
    assert r["unexplained_gaps"] == 0 and r["dups"] == 0
    # synckit.notify: every counted event was taken
    assert r["taken"] == r["notified"] == r["isr_calls"]
    # synckit.ring: every byte written was read, in order
    assert r["ring_read"] == r["ring_written"]
    assert r["ring_written"] + r["ring_dropped"] == r["isr_calls"]
    assert r["ring_breaks"] == 0
    # synckit.critical: the guarded snapshot is never torn, and the section is short
    assert r["reads"] > 1000
    assert r["torn_guarded"] == 0
    assert r["max_cs_us"] < 50, "a critical section around one 64-bit copy took too long"
