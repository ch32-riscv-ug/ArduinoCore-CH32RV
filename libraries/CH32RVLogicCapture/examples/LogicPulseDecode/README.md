# LogicPulseDecode

[日本語](README.ja.md)

Measure a digital signal's frequency and duty cycle from a continuous capture of its GPIO port. The capture runs by DMA; the sketch only scans each completed half for edges, so it shows how to process data while the next half is being filled.

## Operation

As shipped (`SELF_TEST 1`) the sketch drives a 1 kHz, 25 % PWM on the first pin that can make PWM on a timer other than the capture's, and measures that same pin: no wiring. Every second it prints `1000.0 Hz, duty 25.0 %, period 100-100 samples`. Change `TEST_HZ` and `TEST_DUTY` and watch the result follow.

To measure your own signal set `SELF_TEST 0` and feed it to the pin printed at start (`Measuring PA1 ...`), within the pin's voltage limits and with common GND. A held line prints `No edges: held high` / `held low`.

## API and adaptation

- `begin(port, rate)` takes the whole port (16 pins) and `startContinuous(buf, samples)` fills its halves in turn; `readBuffer()` returns a completed half, which stays valid until the DMA comes back to it. `overruns()` counts halves lost because the sketch was too slow, and the output says so.
- At `RATE` = 100 kHz one sample is 10 us: a period is known to one sample, so 1 kHz reads as 100 samples. For a faster signal raise `RATE` (up to `maxContinuousRate()`); a signal slower than half a buffer per period has no full period in one report.
- Edges and counts carry over from one half to the next, so a period that spans the halves is still measured.
- On V00x / X035 the capture uses TIM2, which also drives PWM and `tone()` on some pins; the pin chosen here is on another timer.

## Before running

Output is hardware `Serial` at 115200 baud, 8N1: connect adapter RX to board TX, adapter TX to board RX, and common GND. The generated `sketch.yaml` supplies build profiles. See [build rules](../../../../docs/examples-build-rules.ja.md) and [upload routes](../../../../docs/upload-routes.ja.md).
