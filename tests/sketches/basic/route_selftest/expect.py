"""setRoute()/setPins() on the monitor port itself.

The interesting check moves Serial to a route nobody is listening on and back,
so arriving at the done line at all is the proof. Whether a series has a second
route is a property of the board, so the sketch prints SKIP for what it cannot
reach.

One test function, many checks - the board is asked once and every answer is
read in order. The banner is waited for rather than assumed: the runner attaches after
the board has been flashed and reset, so the sketch repeats
"route_selftest READY" until it is asked (tests/sketches/testcmd.h).
"""


def expect(console) -> None:
    console.expect_exact("route_selftest READY", timeout=20)
    console.write("RUN\n")
    console.expect(r"unknown_route_refused (PASS|SKIP .*)")
    console.expect(r"alive_after_refusal (PASS|SKIP .*)")
    console.expect(r"current_pins_accepted (PASS|SKIP .*)")
    console.expect(r"mixed_route_refused (PASS|SKIP .*)")
    console.expect(r"moved_to_second_route (PASS|SKIP .*)")
    console.expect(r"returned_to_first_route (PASS|SKIP .*)")
    console.expect_exact("route_selftest done failures=0")
