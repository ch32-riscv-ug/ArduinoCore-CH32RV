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
    uv run --env-file .env pytest manual/i2c_loopback/i2c_loopback.py -v -s
"""
CHECKS = [
    "slave_acks_address",
    "other_address_nacked",
    "write_delivered",
    "receive_event_once",
    "receive_count",
    "receive_bytes",
    "request_reply",
    "overread_gets_ff",
    "full_buffer_delivered",
    "second_round_works",
]


def test_i2c_loopback(dut) -> None:
    """
    Expected result (pass):  every check reports PASS (or SKIP on a
                             single-bus series).
    Expected result (fail):  missing pull-ups fail `slave_acks_address` with
                             code 5 (bus timeout); a missing jumper fails it
                             with 2 (address NACK). The code is printed on
                             the FAIL line.
    """
    dut.expect_exact("i2c_loopback READY", timeout=20)
    dut.write("RUN\n")
    for name in CHECKS:
        dut.expect(rf"{name} (PASS|SKIP .*)")
    dut.expect_exact("i2c_loopback done failures=0")
