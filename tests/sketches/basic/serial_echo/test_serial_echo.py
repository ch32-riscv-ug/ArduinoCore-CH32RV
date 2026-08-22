"""Serial receive: the host talks, the target answers.

serial_println only proves the transmit path. This drives the other direction,
so the board's Serial RX pin must be wired to the probe's UART TX as well.

The one sketch with no RUN - receiving *is* what is under test, so its
vocabulary is the test (see tests/TEST_PLAN.ja.md). The banner is waited for
rather than assumed: `dut` is opened after the flashing tool has reset the
board, so the sketch repeats "serial_echo READY" until it is asked
(tests/sketches/testcmd.h).

    uv run pytest sketches/basic/serial_echo --profile ch32x035
"""


def test_serial_echo(dut) -> None:
    dut.expect_exact("serial_echo READY", timeout=20)

    # A line sent to the target comes back with the echo prefix.
    dut.write("ECHO hello\n")
    dut.expect_exact("echo:hello")

    # The target parsed the argument rather than reflecting the bytes.
    dut.write("LEN abcdef\n")
    dut.expect_exact("len=6")

    # Ten lines back to back: the RX ring keeps up and stays aligned.
    for i in range(10):
        dut.write(f"ECHO line{i}\n")
        dut.expect_exact(f"echo:line{i}")

    # Never silence: a host that is out of step has to find out at once.
    dut.write("NOSUCHCOMMAND\n")
    dut.expect_exact("serial_echo unknown cmd=NOSUCHCOMMAND")
