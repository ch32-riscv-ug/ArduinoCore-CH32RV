"""The first exchange with a test sketch: `ready <nonce>`, asked again until `READY <nonce> ...` comes back.

One `ready` and a 15 s wait lost the exchange three ways on the bench (2026-11-21): the console's monitor opening the
probe's UART took longer than the wait on a loaded host (ch32rv's "still connecting to the target"; what was written
before it opened is gone), a sketch built with CH32RV_BOOT_HOLD_MS still held its boot when the line came (X035's
usb-peer-firmware: STARTUP after the line, no READY), and a line written while the DUT reset. Every sketch's `ready`
only prints its reply, so asking again is harmless; a second READY left in the stream matches nothing later.
Staged beside every case's test (run_hardware.py).
"""
import secrets
import time

import pexpect

EVERY_S = 2.0      # a reply takes milliseconds; the time between asks only has to outlast one
WITHIN_S = 45.0    # the monitor's slow open (up to ~20 s seen) and a boot hold on top


def ready(dut, rest=rb"", *, nonce=None, command="ready", timeout=WITHIN_S, every=EVERY_S):
    """`<command> <nonce>` until `READY <nonce><rest>` and the line end come back (rest: the reply's fields, as a
    regular expression with their groups) -> the match. nonce: a fresh one when None."""
    nonce = nonce or secrets.token_hex(4)
    pattern = rb"READY " + nonce.encode() + rest + rb"\r?\n"
    deadline = time.monotonic() + timeout
    asks = 0
    while True:
        dut.write(f"{command} {nonce}")
        asks += 1
        try:
            return dut.expect(pattern, timeout=max(0.1, min(every, deadline - time.monotonic())))
        except pexpect.TIMEOUT:
            if time.monotonic() >= deadline:
                raise AssertionError(f"no READY {nonce} in {timeout:.0f} s ({asks} asks): the DUT or its console "
                                     "did not answer") from None
