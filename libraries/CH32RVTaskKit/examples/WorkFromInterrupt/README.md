# WorkFromInterrupt

日本語: [README.ja.md](README.ja.md)

An interrupt does not do the work, it asks for it: the ISR puts the press time in a `Queue` and calls `post()`; `runOnce()` in `loop()` handles it.

## Hardware and connections

A push button between `BUTTON_PIN` (default `PA1`) and GND, and a 115200-baud 8N1 UART monitor on RX/TX.

## Steps and expected results

1. A press shows `PRESS ms=...` and `RUN handled=1`.
2. Press several times within 300 ms: one `RUN handled=n` drains them all (several posts are one run).

## Settings and limits

Data goes in the `Queue`, the signal is `post()`. Posts are not counted, so anything that must be counted stays in the data.

## API usage and adaptation

`handler.post()` schedules work rather than calling it in the ISR. `runOnce()` executes scheduled work. Use `Queue::trySend()`/`tryReceive()` separately to carry each event's data.

Posting cannot restore data refused by a full queue. Count send failures if needed, and drain queued data when the work runs.

## Related documentation

- [Shared setup](../README.md)
- [CH32RVTaskKit API](../../README.md)

[WorkFromInterrupt.ino](WorkFromInterrupt.ino)
