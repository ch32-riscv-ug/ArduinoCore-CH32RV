"""PIOC programs the bench runs that are copies of an example's: the bench must test the example's program, so the
copy beside the test sketch has to stay the same as the example's (but for the line naming it a copy)."""
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[2]

# (the test sketch's copy, the example's original)
COPIES = [
    ("tests/sketch_support/pioc/ws2812.S", "libraries/CH32RVPioc/examples/PiocWs2812/ws2812.S"),
    ("tests/sketch_support/pioc/uart_tx.S", "libraries/CH32RVPioc/examples/PiocUartTx/uart_tx.S"),
    ("tests/sketch_support/pioc/ir_nec.S", "libraries/CH32RVPioc/examples/PiocIrNec/ir_nec.S"),
    ("tests/sketch_support/pioc/pulse_in.S", "libraries/CH32RVPioc/examples/PiocPulseIn/pulse_in.S"),
    ("tests/sketch_support/pioc/uart_rx.S", "libraries/CH32RVPioc/examples/PiocUartRx/uart_rx.S"),
    ("tests/sketch_support/pioc/spi_tx.S", "libraries/CH32RVPioc/examples/PiocSpiTx/spi_tx.S"),
    ("tests/sketch_support/pioc/nec_rx.S", "libraries/CH32RVPioc/examples/PiocIrNecRx/nec_rx.S"),
    ("tests/sketch_support/pioc/onewire.S", "libraries/CH32RVPioc/examples/PiocOneWire/onewire.S"),
]


def test_copies_match_the_examples():
    for copy, original in COPIES:
        lines = (ROOT / copy).read_text(encoding="utf-8").splitlines()
        lines = [line for line in lines if "A copy of " not in line]
        assert lines == (ROOT / original).read_text(encoding="utf-8").splitlines(), \
            f"{copy} differs from {original}: copy the example's again"
