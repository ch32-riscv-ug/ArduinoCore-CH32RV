# LogicUhsif

[日本語](README.ja.md)

Captures the 16 UHSIF data lines at 200 MS/s and measures the frequency and
duty cycle of a PWM on one of them, DATA9 (PC6). CH32H415 / H416 / H417.

## What you need and wiring

The board and a UART monitor. PC6 is UHSIF DATA9 and can also give a TIM3
PWM, so the sketch measures its own PWM; no wiring is needed.

To measure an outside signal, drop the `analogWrite()` on PC6, connect the
signal to one of DATA0-15 and set `DATA_BIT` to match (the pin table is in
the library README). Inputs go up to the pin's own supply.

## Operation and expected result

One line a second:

```
PWM 390625.0 Hz, duty 25.0 %, 512.00 samples a period (halves 12238, skipped 12235)
```

- The frequency comes from the whole periods between the first and the
  last rising edge. The PWM and the capture run from the same clock, so a
  period is exactly 512 samples.
- halves counts the halves filled, skipped the ones the sketch did not
  take. The sketch takes one a second, so a growing skipped is normal here.
- `UHSIF capture did not start`: check that the board is an H41x and the
  buffer's size and alignment.
- `no signal on DATA9 (PC6)`: the half taken had fewer than two rising
  edges.

## API and flow

`logic.begin(UHSIF_DATA, 200000000)` picks the UHSIF data lines, and
`startContinuous(buf, 32768)` keeps filling the two halves of 16384 samples
in turn. `readBuffer()` returns a filled half. Bit n of a sample is DATAn.

## Notes for changes

- At 200 MS/s a half (16384 samples) is written over again after 82 us. To
  use every half, finish with it in that time, or hand it on as it is (to
  USB, for instance).
- At lower rates `begin()` takes the nearest SYSCLK / n (n = 2-64); check
  `sampleRate()`.
- Make a half last 20 us or more; samples a multiple of 64, the buffer
  32-byte aligned.

## Related

- [Sketch](LogicUhsif.ino)
- [Library API](../../README.md)
- [CH32RVUhsif](../../../CH32RVUhsif/README.md)
- [Examples guide](../../../../docs/examples.md)
