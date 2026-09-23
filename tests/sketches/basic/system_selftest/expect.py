"""CH32.restart(), resetReason() and the watchdog, across two real resets.

One linear script for every board: after BITE the reason is `watchdog` where
the family's LSI frequency is known, and `software` where the sketch had to
substitute restart() (X033/X035, whose F_LSI is missing from the device data
- requested upstream). Which of the two applies is pinned by the
wdt_enable_honest check inside RUN, so the regex below is not a loophole.

The silence after "biting" is deliberate: the next banner is the new boot's,
which is what makes waiting for it race-free.
"""


def expect(console) -> None:
    console.expect_exact("system_selftest READY", timeout=20)
    console.write("RUN\n")
    console.expect(r"reset_reason=\w+")
    console.expect_exact("reason_stable PASS")
    console.expect_exact("wdt_enable_honest PASS")
    console.expect(r"wdt_survives_fed (PASS|SKIP .*)")
    console.expect_exact("system_selftest done failures=0")

    console.write("REBOOT\n")
    console.expect_exact("rebooting")
    console.expect_exact("system_selftest READY", timeout=20)
    console.write("RUN\n")
    console.expect_exact("reset_reason=software")
    console.expect_exact("system_selftest done failures=0")

    console.write("BITE\n")
    console.expect_exact("biting")
    console.expect_exact("system_selftest READY", timeout=20)
    console.write("RUN\n")
    console.expect(r"reset_reason=(watchdog|software)")
    console.expect_exact("system_selftest done failures=0")
