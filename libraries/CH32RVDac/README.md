# CH32RVDac

[日本語](README.ja.md)

The DAC beyond `analogWrite()`: the converter's own triangle and noise, and samples played by DMA, each step on a
timer's trigger (TRGO), so the CPU does nothing while they run. V30x, V4x7 and H41x (the series with a DAC).

```cpp
#include <CH32RVDac.h>
CH32RVDac dac(1);                  // DAC1 (CH32RV_DAC1_PIN: PA4)
dac.begin();
dac.triangle(12, 100000);          // 0 .. 4095 and back, a step every 10 us
dac.noise(8, 50000);               // 8 bits of the converter's LFSR, 50 kHz
dac.play(sine, 64, 64000);         // 64 samples, 1 kHz, round and round
dac.write(2048);                   // a plain value again (the wave stops)
```

## API

| Function | What it does |
|---|---|
| `begin()` / `end()` | The pad to the DAC, its output buffer on, at 0 / the wave stopped and the converter off |
| `write(value)` | 0 .. 4095 now; a running wave stops first |
| `triangle(bits, stepHz, base)` | base .. base + 2^bits - 1 and back, one LSB a step; a full 12-bit triangle is stepHz / 8190 Hz |
| `noise(bits, stepHz, base)` | The converter's LFSR's low `bits` bits added to base, stepHz times a second |
| `play(samples, count, sampleHz)` | 12-bit right-aligned samples by DMA, over and over; the array must stay valid while it plays (a change plays as is) |
| `playBoth(pairs, count, sampleHz)` | Both channels updated together on one trigger (the dual holding register RD12BDHR, by DMA). `pairs` is DAC1, DAC2, DAC1, DAC2, ..., `count` pairs, 4-byte aligned. Called on DAC1's object; it sets DAC2 up itself |
| `stop()` | The wave stops; the output keeps its last value (both after `playBoth()`) |
| `stepRate()` / `running()` | The steps a second the timer actually makes (the nearest it can) / whether a wave runs |

## How it works, and limits

- The step comes from one of the timers the DAC can trigger on (the variant's `CH32RV_DAC_TRIGGERS`: TIM6, TIM7 first,
  then TIM8, TIM5, TIM2, TIM4), held whole while a wave runs.
- `play()` uses the variant's `CH32RV_DACn_DMA` channel (DMA2 ch3 / ch4 on V30x / V4x7, DMAMUX on H41x) while it plays.
- With `triangle()`, `noise()` and `play()` the two channels each run on their own timer; `playBoth()` changes both together.
- On a series without a DAC every function returns false.

## Test

`analog::test_dac_waves` (tests/instrumented/analog) reads the triangle, the noise and played samples back with the
same chip's ADC and judges their range, the number of changes and the timer's rate. `analog::test_dac_both` reads
`playBoth()`'s two opposite squares with ADC1 and ADC2 converting simultaneously: the sum holds in every round (they
change together). Both pass on V407 and H417 (2026-11-19).
