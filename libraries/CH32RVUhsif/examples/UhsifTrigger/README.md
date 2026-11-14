# UhsifTrigger

[日本語](README.ja.md)

Waits for a rising edge on DATA2 with the data comparator (`setDataMatch()`), then takes 4096 samples of DATA0-7, one byte each (`setBusWidth(8)`), once. The capture starts exactly at the edge. For CH32H415 / H416 / H417.

## What you need and wiring

The board and a UART monitor. No wiring: the sketch puts a timer PWM (100 kHz, duty 1/4) on PD12 (DATA2) and captures it.

## Operation and expected result

One line a second. With SYSCLK at 400 MHz the rate is 200 MS/s.

```
first sample 44, high 500 samples, period 2000 samples (100.0 kHz at 200000000 Hz), unwritten 0
```

- Bit 2 of `first sample` is 1 (taken from just after the rising edge); the other bits are whatever the unconnected pins read.
- `high` is the duty-1/4 time, `period` the period, in samples.
- `unwritten` counts samples never written: 0. The DMA writes 64-byte bursts, so the end of a burst stays in the block once the machine stops pushing; the sketch pushes 64 bytes more.

## API and flow

1. `begin(1)` clocks at SYSCLK / 2; `setBusWidth(8)` makes DATA0-7 one byte a sample.
2. `setDataMatch(1 << 2, 1 << 2)`: the comparator (`DataMatch`) is true while DATA2 is 1.
3. The machine waits for DATA2 to be 0 (states 0 / 1), then 1 (2 / 3), pushes and counts every clock (4 / 5) and stops at the limit (6 / 7). Every step is two states (a transition into the state it leaves does not decide again).
4. `setRing(0, capture, 4096, 1, 4096)` writes one buffer; the sketch waits and calls `stop()`.

## Notes for changes

- Waiting for 0 and then 1 makes it an edge trigger; waiting for 1 alone starts in the middle of a high level.
- To trigger on pins off the data bus (CTRL0-15 = PB14-PB10, PE15-PE7, PF13, PF12), use `setControlMatch()` and `ControlMatch`.
- For a continuous ring, use two threads so the socket never stops between buffers (as CH32RVLogicCapture does). The trigger's position: raise `Interrupt` on the trigger transition and read `bytesWritten()`.

## Related

- [Sketch](UhsifTrigger.ino)
- [Library API](../../README.md)
- [The UHSIF analysis (Japanese)](../../../../docs/h417-uhsif.ja.md)
