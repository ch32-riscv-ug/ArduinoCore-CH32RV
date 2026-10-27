# RotaryEncoder

[日本語](README.ja.md)

Count a quadrature rotary encoder with a timer's encoder mode, and print the position as it changes. The timer does the counting; `getCount()` returns a 64-bit count.

## Operation

As shipped (`SELF_TEST 1`) the sketch turns a pretend encoder by driving its two pins itself: no wiring. Every second it turns 40 steps forward or 25 back, by turns, so it prints `count 40`, `count 15`, `count 55`, `count 30` ...

For a real encoder set `SELF_TEST 0` and connect A and B to the two pins printed at start (`Encoder A PA6, B PA7`), and the common pin to GND. The pins are pulled up, so no resistors are needed. Turn it and the count changes; if it goes the wrong way, swap A and B.

## API and adaptation

- `attachFullQuad()` gives 4 counts per cycle (often one detent). For one count per detent use `attachSingleEdge()`, or divide by 4.
- `setFilter(F_CPU / 250000)` ignores pulses shorter than about 4 us; raise it for an encoder that bounces a lot (up to 1024 cycles).
- The pins are chosen automatically: channels 1 and 2 of one timer. To choose them yourself put channels 1 and 2 of one timer of your series in `pinA` / `pinB` (see the [library README](../../README.md)).

## Before running

Output is hardware `Serial` at 115200 baud, 8N1: connect adapter RX to board TX, adapter TX to board RX, and common GND. The generated `sketch.yaml` supplies build profiles. See [build rules](../../../../docs/examples-build-rules.ja.md) and [upload routes](../../../../docs/upload-routes.ja.md).
