# CriticalSection

[日本語](README.ja.md)

Demonstrate copying interrupt-shared values inside a short interrupt-disabled section.

## Hardware and connections

Connect a UART monitor; no external input is required.

## Steps and expected results

The sketch prints `counter`, `seconds` and `ticks`. These particular variables are updated in `loop()`, so it demonstrates the pattern rather than creating a real ISR race.

## API and processing flow

Copy only the values requiring a consistent snapshot between `noInterrupts()` and `interrupts()`. Print afterward. `volatile` prevents the compiler from omitting accesses to ISR-modified values.

## Adapting the example

This pair does not manage nesting. For reusable code, see [SyncKit CriticalSection](../../../CH32RVSyncKit/examples/CriticalSection/README.md), which restores the previous state. Do not call `delay()` or waiting output while interrupts are disabled.

## Related documentation

- [Sketch](CriticalSection.ino)
- [Basic example setup and index](../README.md)
- [Example guide](../../../../docs/examples.md)
