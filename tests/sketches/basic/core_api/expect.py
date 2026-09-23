"""Core API checks that need no external wiring.

The sketch decides pass/fail on the target and prints one line per check, so a
failure here points at a specific API rather than at a value comparison done on
the host.

Print's number formatting is next door in print_format, because its float path
costs 9.4 KB and had this sketch at 97% of a CH32V003.

One test function, many checks - the board is asked once and every answer is
read in order. The banner is waited for rather than assumed: the runner attaches after
the board has been flashed and reset, so the sketch repeats
"core_api READY" until it is asked (tests/sketches/testcmd.h).
"""


def expect(console) -> None:
    console.expect_exact("core_api READY", timeout=20)
    console.write("RUN\n")
    console.expect_exact("millis PASS")
    console.expect_exact("micros PASS")
    console.expect_exact("digital PASS")
    console.expect_exact("pin_encoding PASS")
    console.expect_exact("analogRead PASS")
    console.expect_exact("adc_channel PASS")
    console.expect_exact("analogWrite PASS")
    console.expect_exact("attachInterrupt PASS")
    console.expect_exact("detachInterrupt PASS")
    console.expect_exact("shiftOut PASS")
    console.expect_exact("pulseIn_timeout PASS")
    console.expect_exact("random_repeatable PASS")
    console.expect_exact("random_range PASS")
    console.expect_exact("digitalPinToPort PASS")
    console.expect_exact("digitalPinToBitMask PASS")
    console.expect_exact("portOutputRegister PASS")
    console.expect_exact("portInputRegister PASS")
    console.expect_exact("core_api done failures=0")
