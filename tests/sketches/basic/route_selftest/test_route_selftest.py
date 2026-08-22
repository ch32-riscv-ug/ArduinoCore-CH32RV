"""setRoute()/setPins() on the monitor port itself.

The interesting check moves Serial to a route nobody is listening on and back,
so arriving at the done line at all is the proof. Whether a series has a second
route is a property of the board, so the sketch prints SKIP for what it cannot
reach.

One test function, many checks - the board is asked once and every answer is
read in order. The banner is waited for rather than assumed: `dut` is opened
after the flashing tool has reset the board, so the sketch repeats
"route_selftest READY" until it is asked (tests/sketches/testcmd.h).

    uv run pytest sketches/basic/route_selftest --profile ch32x035
"""


def test_route_selftest(dut) -> None:
    dut.expect_exact("route_selftest READY", timeout=20)
    dut.write("RUN\n")
    dut.expect(r"unknown_route_refused (PASS|SKIP .*)")
    dut.expect(r"alive_after_refusal (PASS|SKIP .*)")
    dut.expect(r"current_pins_accepted (PASS|SKIP .*)")
    dut.expect(r"mixed_route_refused (PASS|SKIP .*)")
    dut.expect(r"moved_to_second_route (PASS|SKIP .*)")
    dut.expect(r"returned_to_first_route (PASS|SKIP .*)")
    dut.expect_exact("route_selftest done failures=0")
