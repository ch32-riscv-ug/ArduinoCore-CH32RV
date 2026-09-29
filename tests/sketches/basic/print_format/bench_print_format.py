"""Print formats numbers the way Arduino does.

Its own case rather than part of core_api: `Serial.println(1.5, 2)` pulls the
soft-float routines in and costs 9428 bytes on CH32V003, which had core_api at
97% of a 16 KB part. Keeping it here leaves core_api at 39% and loses no
coverage - sync_profiles.py gives both the same board list.

Every string below is what cores/arduino/api/Print.cpp says it should print.

Bench test (not test_*: it needs a board behind a port). Run with the bench's .env:

  uv run --env-file .env --with pytest-embedded-arduino-cli-ch32rv \
    pytest -o python_files='bench_*.py' sketches/basic --profile <profile>
"""
from loader import load

kit = load("tests/sketches/bench_kit.py", "bench_kit")


def test_print_format(dut):
    kit.start(dut, 'print_format')
    dut.write("RUN\n")
    # Uppercase hex with no prefix, a negative decimal, two decimals.
    dut.expect_exact("fmt=FF,-42,1.50")
    # Rounds at the requested digit rather than truncating.
    dut.expect_exact("round=3.1416")
    # Print.cpp's own example: 1.999 at two digits carries into the integer.
    dut.expect_exact("carry=2.00")
    # Zero digits: rounded, and no trailing decimal point.
    dut.expect_exact("digits=3")
    dut.expect_exact("print_format done failures=0")
