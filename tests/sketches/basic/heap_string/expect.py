"""The heap works: String, malloc/free and out of memory.

The bug this guards against was total - a sketch with a global String printed
nothing at all, because libgloss's semihosting _sbrk trapped to the reset vector
before .init_array finished. That failure now shows up as a banner that never
arrives, which says "the sketch is not running" rather than "a line is missing".

One test function, many checks - the board is asked once and every answer is
read in order. The banner is waited for rather than assumed: the runner attaches after
the board has been flashed and reset, so the sketch repeats
"heap_string READY" until it is asked (tests/sketches/testcmd.h).
"""


def expect(console) -> None:
    console.expect_exact("heap_string READY", timeout=20)
    console.write("RUN\n")
    console.expect_exact("heap test start")
    console.expect_exact("string=abcdef")
    console.expect_exact("length=6")
    # RAM between the end of .bss and the stack region, and it holds.
    console.expect_exact("malloc=in range")
    console.expect_exact("readback=ok")
    # Eight alloc/free rounds must not walk the program break.
    console.expect_exact("free_returns_memory=ok")
    # An impossible request returns NULL rather than hanging.
    console.expect_exact("oom=null")
    console.expect_exact("heap test done")
    console.expect_exact("heap_string done failures=0")
