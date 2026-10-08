"""CH32RV.restart(), resetReason() and the watchdog, across two real resets.

One linear script for every board: after BITE the reason is `watchdog` where
the family's LSI frequency is known, and `software` where the sketch had to
substitute restart() (X033/X035, whose F_LSI is missing from the device data
- requested upstream). Which of the two applies is pinned by the
wdt_enable_honest check inside RUN, so the regex below is not a loophole.

The silence after "biting" is deliberate: the next banner is the new boot's,
which is what makes waiting for it race-free.

Bench test (not test_*: it needs a board behind a port). Run with the bench's .env:

  uv run --env-file .env --with pytest-embedded-arduino-cli-ch32rv \
    pytest bench --profile <profile>
"""
from loader import load

kit = load("tests-legacy/bench/bench_kit.py", "bench_kit")


def test_system_selftest(dut):
    kit.start(dut, 'system_selftest')
    dut.write("RUN\n")
    dut.expect(r"reset_reason=\w+")
    dut.expect_exact("reason_stable PASS")
    dut.expect_exact("wdt_enable_honest PASS")
    dut.expect(r"wdt_survives_fed (PASS|SKIP)")
    dut.expect_exact("system_selftest done failures=0")

    dut.write("REBOOT\n")
    dut.expect_exact("rebooting")
    dut.expect_exact("system_selftest READY", timeout=20)
    dut.write("RUN\n")
    dut.expect_exact("reset_reason=software")
    dut.expect_exact("system_selftest done failures=0")

    dut.write("BITE\n")
    dut.expect_exact("biting")
    dut.expect_exact("system_selftest READY", timeout=20)
    dut.write("RUN\n")
    dut.expect(r"reset_reason=(watchdog|software)")
    dut.expect_exact("system_selftest done failures=0")
