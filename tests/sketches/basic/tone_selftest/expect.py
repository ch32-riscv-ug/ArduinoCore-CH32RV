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
"tone_selftest READY" until it is asked (tests/sketches/testcmd.h).
"""


def expect(console) -> None:
    console.expect_exact("tone_selftest READY", timeout=20)
    console.write("RUN\n")
    console.expect(r"tone_toggles_pin (PASS|SKIP .*)")
    console.expect(r"tone_rate_plausible (PASS|SKIP .*)")
    console.expect(r"invalid_pin_ignored (PASS|SKIP .*)")
    console.expect(r"notone_stops (PASS|SKIP .*)")
    console.expect(r"notone_leaves_low (PASS|SKIP .*)")
    console.expect(r"duration_plays (PASS|SKIP .*)")
    console.expect(r"duration_stops (PASS|SKIP .*)")
    console.expect(r"duration_leaves_low (PASS|SKIP .*)")
    console.expect(r"restart_plays (PASS|SKIP .*)")
    console.expect_exact("tone_selftest done failures=0")
