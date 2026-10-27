# AdcStreamStats

日本語: [README.ja.md](README.ja.md)

Captures one analog pin continuously by DMA and prints, each second, how many values arrived, their minimum, maximum and mean, and any lost halves.

## Hardware and connections

Put the voltage to measure on `INPUT_PIN` (default `A0`), 0 V to the supply. With nothing connected it still runs, with undefined values. A 115200-baud 8N1 UART monitor on `RX` / `TX`.

## Steps and expected results

1. Each second: `ADC_STREAM rate=... samples/s=... halves=... overruns=... min=... max=... mean=...`.
2. `rate` is the step `begin()` measured and chose, `samples/s` what actually arrived; they agree (2026-10-27: 47,654 and 47,616 on a CH32V307, 250,000 and 249,088 on a CH32X035).
3. With `INPUT_PIN` on GND `mean` is near 0; on the supply, near the top (4095 at 12 bits).

## Settings and limits

- `WANTED_RATE` is conversions per second; the fastest step at or below it is taken. Nothing is slower than the slowest step, so some families run faster than asked (a CH32X035 cannot go below 250,000).
- At high rates the next half can arrive before `loop()` has finished the last, and `overruns` counts up (about 6 a second at 250,000 on a CH32X035). Do less per half, or make `SAMPLES` larger.
- Values are the converter's own, not scaled to `analogRead()`'s 0 .. 1023.

## Related documentation

- [CH32RVAdcStream API](../../README.md)

[AdcStreamStats.ino](AdcStreamStats.ino)
