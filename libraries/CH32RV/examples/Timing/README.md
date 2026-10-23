# Timing

[日本語](README.ja.md)

Demonstrate `millis()`, `micros()` and elapsed-time subtraction across wraparound.

## Hardware and connections

Connect a UART monitor; no external input is needed.

## Steps and expected results

Startup prints a short-delay measurement, followed by elapsed seconds. Measurement includes call and interrupt overhead.

## API and processing flow

Compare unsigned elapsed time as `now - last >= interval`. `millis()` uses ms, `micros()` uses µs, and `delayMicroseconds()` is for short waits.

## Adapting the example

`delayMicroseconds()` busy-waits and blocks sketch progress but does not itself disable interrupts. Split longer work using time comparisons, especially with USB. Subtraction cannot recover elapsed time after leaving the counter unobserved for a complete wrap or more.

## Related documentation

- [Sketch](Timing.ino)
- [Basic example setup and index](../README.md)
- [Example guide](../../../../docs/examples.md)
