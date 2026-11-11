# UhsifStates

[日本語](README.ja.md)

Programs the UHSIF state machine to sample the 16 data lines every clock
(6.25 MHz) into a ring of four buffers. Each time a buffer fills, the
machine raises an interrupt, and the sketch counts them. CH32H415 / H416 /
H417.

## What you need and wiring

The board and a UART monitor. No wiring is needed; a signal on the data
lines (PD10-PD15, PF0-PF2, PC6-PC9, PA13-PA15) shows in the output.

## Operation and expected result

Each second it prints the buffers filled, the bytes written and the newest
sample. With a 400 MHz SYSCLK expect about `3051 buffers/s, 12468224
bytes/s` (6.25 MHz x 2 bytes = 12.5 MB/s; 2048 samples x 3051 = about 6.25
million). Bit n of the sample is DATAn.

If `begin: ...` or `setup: ...` is printed, the name is `lastErrorName()`'s
(see "Errors" in the README).

## API and flow

1. `begin(63)` clocks the UHSIF at SYSCLK / 64.
2. `setStates()` writes two states. Every transition gives `SampleData`
   (latch) and `Push`, so a sample is taken every clock. State 1 leaves by
   its right transition when the data counter reaches its limit (2048 - 2),
   resetting the counter and raising the interrupt.
3. `setRing(0, ring, 4096, 4, 4096)`: thread 0 writes four 4096-byte buffers
   in turn.
4. `start()`.

## Notes for changes

- With one thread, the socket stops after each buffer and loses the samples
  that come before the library's interrupt sends it on. To lose none, use
  two threads in turn (CH32RVLogicCapture's `begin(UHSIF_DATA, rate)`).
- Buffers: a multiple of 64 bytes, 32-byte aligned.
- Set the data counter's limit to a buffer's samples minus 2.
- At faster clocks, keep the interrupts 20 us or more apart.

## Related

- [Sketch](UhsifStates.ino)
- [Library API](../../README.md)
- [Examples guide](../../../../docs/examples.md)
