# NotifyCounter

日本語: [README.ja.md](README.ja.md)

Counts events without data: the ISR calls `notify()`, `loop()` takes them with `tryTakeAll()`.

## Hardware and connections

A push button between `BUTTON_PIN` (default `PA1`) and GND, and a 115200-baud 8N1 UART monitor on RX/TX.

## Steps and expected results

1. Presses are reported every 500 ms as `PRESSES since_last=n`.

## Settings and limits

Use `tryTake()` to handle them one at a time. The counter stops at its maximum instead of wrapping.

## API usage and adaptation

`notify()` increments the count and `tryTakeAll()` removes the current total together. Events arriving while that result is printed remain for the next call.

This counts ISR events, which include button chatter. Debounce for physical press counts. Use QueueFromInterrupt when individual timestamps or ordering also matter.

## Related documentation

- [Shared setup](../README.md)
- [CH32RVSyncKit API](../../README.md)

[NotifyCounter.ino](NotifyCounter.ino)
