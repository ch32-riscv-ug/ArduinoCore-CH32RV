"""The heap works: String, malloc/free, out-of-memory and printf.

The bug this guards against was total - a sketch with a global String printed
nothing at all, because libgloss's semihosting _sbrk trapped to the reset
vector. `test_reaches_setup` is therefore the important one; the rest check the
allocator behaves once it is reachable.

Run against hardware:
    uv run pytest sketches/basic/heap_string --profile ch32x035 --port /dev/ttyACM0

Build only (no hardware, what CI runs):
    uv run pytest sketches/basic/heap_string --profile ch32x035 --run-mode build
"""


def test_reaches_setup(dut) -> None:
    """Printed before any allocation: silence here means the board reset."""
    dut.expect_exact("heap test start")


def test_string_concat(dut) -> None:
    dut.expect_exact("string=abcdef")
    dut.expect_exact("length=6")


def test_malloc_in_range(dut) -> None:
    """malloc must return RAM between the end of .bss and the stack region."""
    dut.expect_exact("malloc=in range")
    dut.expect_exact("readback=ok")


def test_free_returns_memory(dut) -> None:
    """Eight alloc/free rounds of one size must not advance the program break."""
    dut.expect_exact("free_returns_memory=ok")


def test_out_of_memory(dut) -> None:
    """An impossible request returns NULL instead of hanging or resetting."""
    dut.expect_exact("oom=null")


def test_completes(dut) -> None:
    dut.expect_exact("heap test done")
