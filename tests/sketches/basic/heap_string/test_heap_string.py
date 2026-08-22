"""The heap works: String, malloc/free and out of memory.

The bug this guards against was total - a sketch with a global String printed
nothing at all, because libgloss's semihosting _sbrk trapped to the reset vector
before .init_array finished. That failure now shows up as a banner that never
arrives, which says "the sketch is not running" rather than "a line is missing".

One test function, many checks - the board is asked once and every answer is
read in order. The banner is waited for rather than assumed: `dut` is opened
after the flashing tool has reset the board, so the sketch repeats
"heap_string READY" until it is asked (tests/sketches/testcmd.h).

    uv run pytest sketches/basic/heap_string --profile ch32x035
"""


def test_heap_string(dut) -> None:
    dut.expect_exact("heap_string READY", timeout=20)
    dut.write("RUN\n")
    dut.expect_exact("heap test start")
    dut.expect_exact("string=abcdef")
    dut.expect_exact("length=6")
    # RAM between the end of .bss and the stack region, and it holds.
    dut.expect_exact("malloc=in range")
    dut.expect_exact("readback=ok")
    # Eight alloc/free rounds must not walk the program break.
    dut.expect_exact("free_returns_memory=ok")
    # An impossible request returns NULL rather than hanging.
    dut.expect_exact("oom=null")
    dut.expect_exact("heap test done")
    dut.expect_exact("heap_string done failures=0")
