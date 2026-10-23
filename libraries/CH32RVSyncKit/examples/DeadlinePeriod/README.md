# DeadlinePeriod

日本語: [README.ja.md](README.ja.md)

Two schedules at once without `delay()`: a `Period` blinks an LED and a `Deadline` waits 5 s for UART input.

## Hardware and connections

An LED with its resistor on `LED_PIN` (default `PA1`). A 115200-baud 8N1 UART monitor on RX/TX.

## Steps and expected results

1. The LED toggles every 250 ms.
2. After `TYPE something within 5 s`, send something: `THANKS spare_ms=...`, or `TOO_SLOW` after 5 s. The LED keeps blinking throughout.

## Settings and limits

When `loop()` is late by several periods, `due()` is true once and `missed()` counts the skipped ones (no burst). Both are only as punctual as `loop()`.

## API usage and adaptation

`blink.due()` checks the periodic schedule. `answer.arm(ms)` sets a deadline, `expired()` checks it and `remaining()` returns remaining ms. These APIs check time when called; they do not create an interrupting timer.

The example accepts the first received data without waiting for a complete line. To require a complete line by the deadline, combine newline accumulation with the deadline state.

## Related documentation

- [Shared setup](../README.md)
- [CH32RVSyncKit API](../../README.md)

[DeadlinePeriod.ino](DeadlinePeriod.ino)
