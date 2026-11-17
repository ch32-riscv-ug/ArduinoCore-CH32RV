# Blinker

[日本語](README.ja.md)

arduino-esp32's Ticker example, as it is there: one Ticker blinks an LED, a second switches the blinking on and off every 5 seconds, and a third, once, slows it down after 30 seconds. All three share one hardware timer.

## Hardware and connections

An LED with a current-limiting resistor on the pin named `LED` (PA1).

## Steps and expected results

The LED blinks fast (every 0.1 s) for 5 seconds, then stays off for 5 seconds, and so on. After 30 seconds the blinking slows to every 0.5 s.

## API and adaptation

- `attach(seconds, callback)` calls it every period, `once(seconds, callback)` once; `attach_ms()` / `attach_us()` and `once_ms()` / `once_us()` take milliseconds and microseconds. `detach()` stops it, `active()` says whether it runs.
- Callbacks run in the timer's interrupt (on the ESP32, in a task): keep them short and leave `Serial` and `delay()` to `loop()`.
- The Tickers take one timer while any of them runs (`timerBegin()` at 1 MHz). When analogWrite(), tone(), Servo or a library hold every timer, `attach()` does nothing and `active()` is false.

## Before running

The generated `sketch.yaml` supplies build profiles. See [build rules](../../../../docs/examples-build-rules.ja.md) and [upload routes](../../../../docs/upload-routes.ja.md).
