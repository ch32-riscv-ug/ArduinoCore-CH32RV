"""setRoute()/setPins() on the monitor port itself.

The interesting check moves Serial to a route nobody is listening on and back,
so arriving at the done line at all is the proof. Whether a series has a second
route is a property of the board, so the sketch prints SKIP for what it cannot
reach.

One test function, many checks - the board is asked once and every answer is
read in order. The banner is waited for rather than assumed: the runner attaches after
the board has been flashed and reset, so the sketch repeats
"route_selftest READY" until it is asked (tests/sketches/testcmd.h).

Bench test (not test_*: it needs a board behind a port). Run with the bench's .env:

  uv run --env-file .env --with pytest-embedded-arduino-cli-ch32rv \
    pytest -o python_files='bench_*.py' sketches/basic --profile <profile>
"""
from loader import load

kit = load("tests/sketches/bench_kit.py", "bench_kit")


def test_route_selftest(dut):
    kit.start(dut, 'route_selftest')
    dut.write("RUN\n")
    dut.expect(r"unknown_route_refused (PASS|SKIP)")
    dut.expect(r"alive_after_refusal (PASS|SKIP)")
    dut.expect(r"current_pins_accepted (PASS|SKIP)")
    dut.expect(r"mixed_route_refused (PASS|SKIP)")
    dut.expect(r"moved_to_second_route (PASS|SKIP)")
    dut.expect(r"returned_to_first_route (PASS|SKIP)")
    dut.expect_exact("route_selftest done failures=0")
