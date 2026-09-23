"""
Purpose:
    Wire's slave mode against its own master mode, through real wire: the
    data path both ways, the callbacks, the 0xFF over-read filler and the
    buffer cap. The unwired half of the story lives in
    tests/sketches/basic/wire_selftest (a slave with no master stays quiet);
    this is the wired half that actually moves bytes.

Why manual:
    Two jumpers and two pull-up resistors have to be fitted by hand.

Required hardware:
    - One CH32 board whose series bonds both I2C buses. On the bench today:
      CH32V103 / CH32V203 / CH32L103 (I2C1 = PB6/PB7, I2C2 = PB10/PB11).
      CH32X035 has one bus; there the sketch reports SKIPs.
    - Two jumpers:   PB6 - PB10   (SCL to SCL)
                     PB7 - PB11   (SDA to SDA)
    - Pull-ups from each line to 3V3, anything from 2.2 k to 10 k. I2C is
      open drain: without them the lines never rise and every check fails
      with a bounded error, not a hang.

Safety:
    Open-drain both sides, so miswiring the two jumpers between these four
    pads cannot fight a driver. Keep the pull-ups off the SWD pads.

Setup:
    cd tests
    uv run manual/i2c_loopback/i2c_loopback.py [--board <BOARD>]
"""
import pathlib
import sys

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "smoke"))
import smoke  # noqa: E402

if __name__ == "__main__":
    sys.exit(smoke.run_directory(HERE))
