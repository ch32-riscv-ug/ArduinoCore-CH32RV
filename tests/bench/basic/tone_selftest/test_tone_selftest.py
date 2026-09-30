"""tone() drives the pin, at about the right rate, and stops when told.

No speaker is involved: the sketch reads back the pad it is driving, which is
enough to tell playing from silent and to catch a timer left running after
noTone() or after a duration expired.

A series with no timer to spare for tone prints SKIP for every check. That is
the target's decision, not the host's, so SKIP is accepted here - what is never
accepted is a line that does not arrive.

One test function, many checks - the board is asked once and every answer is
read in order. The banner is waited for rather than assumed: the runner attaches after
the board has been flashed and reset, so the sketch repeats
"tone_selftest READY" until it is asked (tests/build/sketches/testcmd.h).

Bench test (not test_*: it needs a board behind a port). Run with the bench's .env:

  uv run --env-file .env --with pytest-embedded-arduino-cli-ch32rv \
    pytest bench --profile <profile>
"""
from loader import load

kit = load("tests/bench/bench_kit.py", "bench_kit")


def test_tone_selftest(dut):
    kit.start(dut, 'tone_selftest')
    dut.write("RUN\n")
    dut.expect(r"tone_toggles_pin (PASS|SKIP)")
    dut.expect(r"tone_rate_plausible (PASS|SKIP)")
    dut.expect(r"invalid_pin_ignored (PASS|SKIP)")
    dut.expect(r"notone_stops (PASS|SKIP)")
    dut.expect(r"notone_leaves_low (PASS|SKIP)")
    dut.expect(r"duration_plays (PASS|SKIP)")
    dut.expect(r"duration_stops (PASS|SKIP)")
    dut.expect(r"duration_leaves_low (PASS|SKIP)")
    dut.expect(r"restart_plays (PASS|SKIP)")
    dut.expect_exact("tone_selftest done failures=0")
