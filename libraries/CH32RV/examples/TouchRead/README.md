# TouchRead

[日本語](README.ja.md)

Read a TouchKey pad with `touchRead()` and say when a finger is on it: a button with no parts. The name and meaning are arduino-esp32's `touchRead()`: the raw count.

## What you need and wiring

A metal pad or a short wire on `PAD` (`A0` by default). It works with nothing attached too, with the finger on the pin itself. A UART monitor.

## Operation and expected result

Keep fingers off for the first half second: that is the untouched value. Then a touch prints `touched` and letting go `released`.

- TouchKey is on V005-V007 / M007, X033 / X035, V20x and V30x so far. Elsewhere `touchRead()` returns 0 and the sketch prints `No TouchKey on this series or this pin`.
- Whether a touch raises or lowers the count can differ by series and board, so the sketch only looks at how far it moved. The direction a finger gives has not been recorded on the bench yet.

## API and flow

- `touchRead(pin)`: the TouchKey count (0 to 4095) of an analog pin. It is set up so that an unconnected pin reads around mid-scale.
- `touchSetCycles(measure, sleep)`: the charging time, in ADC clocks; 0 is the family's default. `sleep` is ESP32's and unused here.
- One measurement borrows ADC1 briefly and waits for it, as `analogRead()` does.

## Adapting it

- A bigger pad moves the count further: raise `THRESHOLD`. If the count sticks at 4095, shorten the charging time with `touchSetCycles()`. If it sticks at 0, lengthen it.
- If supply or noise drifts the count slowly, let `baseline` follow it while nothing is touched.

## Related

- [Sketch](TouchRead.ino)
- [Basic examples: setup and list](../README.md)
