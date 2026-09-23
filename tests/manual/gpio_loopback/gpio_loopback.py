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
    - One CH32 board with a WCH-LinkE attached (flash, and the console over the
      debug link)
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

    The sketch speaks the command protocol (tests/sketches/testcmd.h): setup()
    only announces itself and the checks run when the host sends RUN, so a
    missing jumper fails a named check rather than producing a silence that
    could equally be a board that never booted.

    A pad that does not exist on the package is a compile error naming it, and
    a missing jumper fails `level_through_wire` rather than passing silently -
    the test drives both levels, because a floating input often reads HIGH.
"""
import pathlib
import sys

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
sys.path.insert(0, str(HERE.parent / "smoke"))
from env_config import pin, write_pin_header  # noqa: E402
import smoke                                     # noqa: E402


def main() -> int:
    """The pads come from the environment and are compiled in, so write them first."""
    pins = {
        "LOOPBACK_OUT": pin("CH32_LOOPBACK_OUT", "PA0"),
        "LOOPBACK_IN": pin("CH32_LOOPBACK_IN", "PB0"),
    }
    path = write_pin_header(HERE, pins)
    print(f"{path.name}: " + ", ".join(
        f"{m}={pad} ({n})" for m, (pad, n) in sorted(pins.items())))
    return smoke.run_directory(HERE)


if __name__ == "__main__":
    sys.exit(main())
