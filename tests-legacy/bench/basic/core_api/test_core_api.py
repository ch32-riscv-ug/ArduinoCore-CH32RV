"""Core API checks that need no external wiring.

The sketch decides pass/fail on the target and prints one line per check, so a
failure here points at a specific API rather than at a value comparison done on
the host.

Print's number formatting is next door in print_format, because its float path
costs 9.4 KB and had this sketch at 97% of a CH32V003.

One test function, many checks - the board is asked once and every answer is
read in order. The banner is waited for rather than assumed: the runner attaches after
the board has been flashed and reset, so the sketch repeats
"core_api READY" until it is asked (tests-legacy/build/sketches/testcmd.h).

Bench test (not test_*: it needs a board behind a port). Run with the bench's .env:

  uv run --env-file .env --with pytest-embedded-arduino-cli-ch32rv \
    pytest bench --profile <profile>
"""
from loader import load

kit = load("tests-legacy/bench/bench_kit.py", "bench_kit")


def test_core_api(dut):
    kit.start(dut, 'core_api')
    dut.write("RUN\n")
    dut.expect_exact("millis PASS")
    dut.expect_exact("micros PASS")
    dut.expect_exact("digital PASS")
    dut.expect_exact("pin_encoding PASS")
    dut.expect_exact("analogRead PASS")
    dut.expect_exact("adc_channel PASS")
    dut.expect_exact("analogWrite PASS")
    dut.expect_exact("attachInterrupt PASS")
    dut.expect_exact("detachInterrupt PASS")
    dut.expect_exact("shiftOut PASS")
    dut.expect_exact("pulseIn_timeout PASS")
    dut.expect_exact("random_repeatable PASS")
    dut.expect_exact("random_range PASS")
    dut.expect_exact("digitalPinToPort PASS")
    dut.expect_exact("digitalPinToBitMask PASS")
    dut.expect_exact("portOutputRegister PASS")
    dut.expect_exact("portInputRegister PASS")
    dut.expect_exact("core_api done failures=0")
