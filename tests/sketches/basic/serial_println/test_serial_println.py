"""Milestone 1: Serial.begin/print/println work on every target board.

Run against hardware:
    uv run pytest sketches/basic/serial_println --profile ch32v00x --port /dev/ttyACM0

Build only (no hardware, what CI runs):
    uv run pytest sketches/basic/serial_println --profile ch32v00x --run-mode build
"""


def test_banner(dut) -> None:
    dut.expect_exact("hello from ch32")


def test_print_int(dut) -> None:
    dut.expect_exact("int=42")


def test_print_hex(dut) -> None:
    """Print(value, HEX) must use uppercase without a 0x prefix, as Arduino does."""
    dut.expect_exact("hex=BEEF")
