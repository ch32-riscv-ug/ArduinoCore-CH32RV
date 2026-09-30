"""The USB PD frame logic runs correctly on the target itself.

tests/unit/test_pd_frames.py holds the breadth (it costs nothing to add a
case there); this run proves the same decisions on rv32ec, where a shift or
an integer promotion can behave differently from the host. No PD hardware,
no wiring: the sketch feeds the parser canned capability words.
"""


def expect(console) -> None:
    console.expect_exact("pd_selftest READY", timeout=20)
    console.write("RUN\n")
    console.expect_exact("parse_count PASS")
    console.expect_exact("kinds_in_order PASS")
    console.expect_exact("fixed_9v PASS")
    console.expect_exact("pps_low_range PASS")
    console.expect_exact("first_pdo_flags PASS")
    console.expect_exact("battery_parses PASS")
    console.expect_exact("variable_parses PASS")
    console.expect_exact("avs_is_unknown PASS")
    console.expect_exact("odd_never_requested PASS")
    console.expect_exact("pick_fixed_exact PASS")
    console.expect_exact("pick_pps_between PASS")
    console.expect_exact("pick_pps_current PASS")
    console.expect_exact("pick_refuses PASS")
    console.expect_exact("rdo_fixed PASS")
    console.expect_exact("rdo_pps PASS")
    console.expect_exact("rdo_pps_truncates PASS")
    console.expect_exact("request_caps_current PASS")
    console.expect_exact("header_fields PASS")
    console.expect(r"hw_begin (PASS|SKIP .*)")
    # Printed on parts with the block: which world the bench is in. With a
    # PD supply on the connector, pd_connected=1 and the driver's own 5 V
    # contract is what hw_request_refused (name kept stable) then proves.
    console.expect(r"hw_state_consistent (PASS|SKIP .*)")
    console.expect(r"hw_request_refused (PASS|SKIP .*)")
    console.expect(r"hw_restart (PASS|SKIP .*)")
    console.expect_exact("pd_selftest done failures=0")
