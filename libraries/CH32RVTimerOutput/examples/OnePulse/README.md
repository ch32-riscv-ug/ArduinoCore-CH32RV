# OnePulse

[日本語](README.ja.md)

Make single pulses of an exact width and delay with a timer's one-pulse mode (`CH32RVOnePulse`), and a square wave with output compare (`CH32RVOutputCompare`). The timer times both: the pulse is exact to the timer's tick whatever the CPU does.

## Hardware and connections

No wiring is needed to run it. The sketch picks the pins and prints them at start (`Pulses on PA6: ...`, `500 Hz square on PA7`). Watch them with a logic analyser or an oscilloscope.

## Steps and expected results

Every 100 ms the pulse pin goes high for 20 µs, 50 µs after `fire()`. The second pin toggles 250 µs into each 1000 µs period: a 500 Hz square wave at 50 % duty.

## API and adaptation

- `fire(delayMicros, widthMicros)` starts one pulse now; it is false while one is still running (`busy()`). `begin(pin, HIGH, triggerPin, RISING)` makes an edge on another pin of the same timer (its channel 1 or 2) start the pulse in hardware instead.
- `CH32RVOutputCompare::set(mode, atMicros)` takes `ACTIVE`, `INACTIVE`, `TOGGLE`, `FORCE_ACTIVE`, `FORCE_INACTIVE` or `TIMING`; `onMatch()` runs a callback at each match.

## Before running

Output is hardware `Serial` at 115200 baud, 8N1. The generated `sketch.yaml` supplies build profiles. See [build rules](../../../../docs/examples-build-rules.ja.md) and [upload routes](../../../../docs/upload-routes.ja.md).
