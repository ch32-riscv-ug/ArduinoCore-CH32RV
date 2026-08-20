"""Serial receive: the host talks, the target answers.

serial_println only proves the transmit path. This drives the other direction,
so the board's Serial RX pin must be wired to the probe's UART TX as well.

    uv run pytest sketches/basic/serial_echo --profile ch32x035
"""


def test_banner(dut) -> None:
    dut.expect_exact("echo ready")


def test_echo(dut) -> None:
    """A line sent to the target comes back with the echo prefix."""
    dut.write("hello\n")
    dut.expect_exact("echo:hello")


def test_command(dut) -> None:
    """The target parses what it received, not just mirrors bytes."""
    dut.write("ping\n")
    dut.expect_exact("pong")


def test_repeated(dut) -> None:
    """Several lines in a row: the RX ring buffer keeps up and stays aligned."""
    for i in range(10):
        dut.write(f"line{i}\n")
        dut.expect_exact(f"echo:line{i}")
