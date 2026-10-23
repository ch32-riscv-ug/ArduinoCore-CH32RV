# BasicWork

日本語: [README.ja.md](README.ja.md)

Two pieces of work that run only on their trigger: an LED on a `Period`, and a line reader when the UART has bytes. `loop()` is one `runOnce()`, the one place either runs.

## Hardware and connections

An LED with its resistor on `LED_PIN` (default `PA1`), and a 115200-baud 8N1 UART monitor on RX/TX.

## Steps and expected results

1. `BasicWork ready`, and the LED toggles every 250 ms.
2. Send a line: `LINE` comes back, and the LED keeps blinking.

## Settings and limits

An attached Work runs once in the first `runOnce()`, then only on a trigger (`wakeEvery`, `wakeWhen`, `wakeOn`, `post()`). A Work cannot wait in the middle: it keeps its state in variables and returns.

## API usage and adaptation

`Work` associates a short ordinary function with wake conditions. Register it with `app.attach(work)`. `wakeEvery(period)` selects a schedule and `wakeWhen(predicate)` a condition; execution happens in `app.runOnce()`.

A true receive condition does not mean a complete line is present. Preserve partial input and return rather than waiting for a newline inside the work function. Keep Work and condition objects alive while the executor uses them.

## Related documentation

- [Shared setup](../README.md)
- [CH32RVTaskKit API](../../README.md)

[BasicWork.ino](BasicWork.ino)
