"""The USB PD frame logic runs correctly on the target itself.

tests/unit/test_pd_frames.py holds the breadth (it costs nothing to add a
case there); this run proves the same decisions on rv32ec, where a shift or
an integer promotion can behave differently from the host. No PD hardware,
no wiring: the sketch feeds the parser canned capability words.
"""


def test_pd_selftest(dut) -> None:
    dut.expect_exact("pd_selftest READY", timeout=20)
    dut.write("RUN\n")
    dut.expect_exact("parse_count PASS")
    dut.expect_exact("kinds_in_order PASS")
    dut.expect_exact("fixed_9v PASS")
    dut.expect_exact("pps_low_range PASS")
    dut.expect_exact("first_pdo_flags PASS")
    dut.expect_exact("battery_parses PASS")
    dut.expect_exact("variable_parses PASS")
    dut.expect_exact("avs_is_unknown PASS")
    dut.expect_exact("odd_never_requested PASS")
    dut.expect_exact("pick_fixed_exact PASS")
    dut.expect_exact("pick_pps_between PASS")
    dut.expect_exact("pick_pps_current PASS")
    dut.expect_exact("pick_refuses PASS")
    dut.expect_exact("rdo_fixed PASS")
    dut.expect_exact("rdo_pps PASS")
    dut.expect_exact("rdo_pps_truncates PASS")
    dut.expect_exact("request_caps_current PASS")
    dut.expect_exact("header_fields PASS")
    dut.expect_exact("pd_selftest done failures=0")
