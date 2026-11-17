# EdgeTimes

[日本語](README.ja.md)

Time every edge of a signal with a timer's input capture (`CH32RVInputCapture`), and print the high and low times between them. The hardware latches each edge's count, so the times are exact to the tick (1 µs here) however late the interrupt runs.

## Operation

As shipped (`SELF_TEST 1`) the sketch plays a 1 kHz square wave with `tone()` on the measured pin and times that same pin: no wiring. Every half second it prints a line such as `high 500 us  low 500 us  high 500 us ...`.

To time your own signal set `SELF_TEST 0` and feed it to the pin printed at start (`Timing PA6` or the like), within the pin's voltage limits and with common GND. A still line prints `No edges: the line is still`.

## API and adaptation

- `begin(pin, CHANGE, 1000000, INPUT_PULLDOWN)` takes both edges at 1 MHz. `RISING` or `FALLING` alone gives the period between edges of one kind. Another `tickHz` changes the resolution.
- `read()` returns the queued edges oldest first (8 at most); `onEdge()` takes a callback instead, run from the timer's interrupt for each edge.
- The pin is chosen automatically: a timer channel other than `tone()`'s, any channel and any route. See the [library README](../../README.md).

## Before running

Output is hardware `Serial` at 115200 baud, 8N1: connect adapter RX to board TX, adapter TX to board RX, and common GND. The generated `sketch.yaml` supplies build profiles. See [build rules](../../../../docs/examples-build-rules.ja.md) and [upload routes](../../../../docs/upload-routes.ja.md).
