# PulseMeasure

[日本語](README.ja.md)

Measure a signal's frequency, period and pulse width with a timer's PWM-input mode. The timer does the counting; every half second the sketch reads the newest whole period. No interrupt, no sampling of the pin.

## Operation

As shipped (`SELF_TEST 1`) the sketch plays a 1 kHz square wave with `tone()` on the measured pin and measures that same pin: no wiring. Every half second it prints `1000 Hz, period 1000 us, high 500 us, duty 50 %`. Change `TEST_HZ` and watch the result follow.

To measure your own signal set `SELF_TEST 0` and feed it to the pin printed at start (`Measuring PA6 ...`), within the pin's voltage limits and with common GND. A held line prints `No signal: held high` / `held low`.

## API and adaptation

- `MIN_HZ` in `begin(pin, MIN_HZ, INPUT_PULLDOWN)` is the slowest signal to measure, and it sets the resolution: raise it to measure fast signals finely, lower it for slow ones. A signal slower than `MIN_HZ` is not measured and `stalled()` is true.
- The pin is chosen automatically: channel 1 or 2 of a timer other than `tone()`'s. To choose it yourself put a channel 1 / 2 pad of your series in `pin` (see the [library README](../../README.md)).
- The arithmetic is integer so that it fits the V003's flash; `Pulse::frequency()` and `duty()` return floats.

## Before running

Output is hardware `Serial` at 115200 baud, 8N1: connect adapter RX to board TX, adapter TX to board RX, and common GND. The generated `sketch.yaml` supplies build profiles. See [build rules](../../../../docs/examples-build-rules.ja.md) and [upload routes](../../../../docs/upload-routes.ja.md).
