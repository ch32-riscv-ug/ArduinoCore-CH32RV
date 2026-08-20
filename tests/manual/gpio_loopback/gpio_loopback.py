"""
Purpose:
    Verify GPIO the only way that proves the pad actually drives something: a
    wire between two pins on different ports. Covers the level, both pull
    resistors, an EXTI edge across ports, and analogWrite's duty cycle measured
    with pulseIn.

Why manual:
    It needs a jumper wire fitted by hand, and which two pads are free depends
    on the board on the bench. `core_api` covers what can be checked without
    wiring; this covers what cannot.

Required hardware:
    - One CH32 board with a WCH-LinkE attached (flash + Serial over one cable)
    - One jumper wire between the two pads passed as CH32_LOOPBACK_OUT and
      CH32_LOOPBACK_IN

Safety:
    Both pads are driven push-pull. Pick pads that are not wired to anything
    else on the board - shorting an output to a supply rail or to another
    driver damages the part. Do not use the SWD pads (PA13/PA14, or PC18/PC19
    on X033/X035): driving them kills the debug connection mid-run.

Setup:
    1. Choose two free pads on different ports, e.g. PA0 and PB0.
    2. Fit the jumper between them.
    3. Put them in tests/.env (copy .env.example):

           CH32_LOOPBACK_OUT=PA0
           CH32_LOOPBACK_IN=PB0

    4. Run:

       cd tests
       uv run --env-file .env pytest manual/gpio_loopback/gpio_loopback.py -v -s

    A pad that does not exist on the package is a compile error naming it, and
    a missing jumper fails `level_through_wire` rather than passing silently -
    the test drives both levels, because a floating input often reads HIGH.
"""
import pytest

CHECKS = [
    "pins_differ",
    "pins_valid",
    "level_through_wire",
    "pullup",
    "pulldown",
    "exti_cross_port",
    "pwm_duty_25pct",
    "pwm_duty_75pct",
    "pwm_duty_ordered",
]


def test_starts(dut) -> None:
    """
    Expected result (pass):  the sketch prints its banner and the pads it uses.
    Expected result (fail):  nothing arrives - check the Serial wiring first,
                             with `uv run tests/manual/smoke/smoke.py --board <board>`.
    """
    dut.expect_exact("gpio_loopback begin")


@pytest.mark.parametrize("check", CHECKS)
def test_check(dut, check: str) -> None:
    """Each check prints "<name> PASS" or "<name> FAIL <detail>"."""
    dut.expect_exact(f"{check} PASS")


def test_no_failures(dut) -> None:
    dut.expect_exact("gpio_loopback done failures=0")
