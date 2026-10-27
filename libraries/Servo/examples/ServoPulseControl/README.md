# ServoPulseControl

[日本語](README.ja.md)

Drive a servo from the serial console: by pulse width in microseconds or by angle, and detach or attach it. The next step after [Sweep](../Sweep/README.md): it shows the pulse range a servo accepts and what detaching does.

## Hardware and connections

Connect the servo's signal to `SERVO_PIN` (PA1), power the servo from its own supply (4.8-6 V for most) and share GND with the board. Do not power it from the board's 3V3. A 3.3 V signal drives most servos; check yours.

## Operation

Open the console at 115200 baud with CR or LF line endings. At start the servo goes to the middle, `1500 us = 90 deg`. Then:

- `1200` - a pulse of 1200 us (`1200 us = 36 deg`). Values outside `MIN_US`..`MAX_US` are clamped (`3000` gives 2000).
- `a180` - an angle: 0 and 180 degrees are `MIN_US` and `MAX_US`.
- `d` - detach: the pulses stop and the pin stays low, so the servo no longer holds its position.
- `r` - attach again; the pulse restarts at the middle.

## API and adaptation

`attach(pin, min, max)` fixes the pulse range. Set `MIN_US` / `MAX_US` from the servo's data: many take 1000-2000 us, some 500-2500; pushing past the end stop strains the gears. `writeMicroseconds()` is the precise form, `write()` maps degrees into the range. The timer keeps sending a pulse every 20 ms by itself, so `loop()` is free to read the console. `attach()` returns `INVALID_SERVO` when no timer is free on the series.

Measured on a CH32V103 with CH32RVLogicCapture on the same pin (2026-10-28): 1000, 1500, 2000 and 2400 us came out at those widths, 544 us at 550 (10 us sampling).

## Before running

Log output is hardware `Serial` at 115200 baud, 8N1: connect adapter RX to board TX, adapter TX to board RX, and common GND. The generated `sketch.yaml` supplies build profiles. See [build rules](../../../../docs/examples-build-rules.ja.md) and [upload routes](../../../../docs/upload-routes.ja.md).
