"""Core API checks that need no external wiring.

The sketch decides pass/fail on the target and prints one line per check, so a
failure here points at a specific API rather than at a value comparison done on
the host.

One test function, many checks - the board is asked once and every answer is
read in order. The banner is waited for rather than assumed: `dut` is opened
after the flashing tool has reset the board, so the sketch repeats
"core_api READY" until it is asked (tests/sketches/testcmd.h).

    uv run pytest sketches/basic/core_api --profile ch32v203
"""


def test_core_api(dut) -> None:
    dut.expect_exact("core_api READY", timeout=20)
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
    # Printed here, between the checks, and both this runner and pexpect match
    # forward only - so the order below has to be the sketch's order.
    # HEX is uppercase without a prefix, and floats honour the digit count.
    dut.expect_exact("fmt=FF,-42,1.50")
    dut.expect_exact("availableForWrite PASS")
    dut.expect_exact("digitalPinToPort PASS")
    dut.expect_exact("digitalPinToBitMask PASS")
    dut.expect_exact("portOutputRegister PASS")
    dut.expect_exact("portInputRegister PASS")
    dut.expect_exact("core_api done failures=0")
