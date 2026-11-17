# CH32RVAdcStream

[日本語](README.ja.md)

Keeps ADC1 converting at a steady rate - free-running, on a timer's tick or on a pin's edge - and moves the results by DMA into two buffers in turn. Instead of calling `analogRead()` over and over, the conversions never stop and the sketch takes each full half. The API has the shape of [CH32RVHsAdc](../CH32RVHsAdc/README.md), the H41x's high-speed ADC library.

```cpp
#include <CH32RVAdcStream.h>

CH32RVAdcStream adc;
static uint16_t buffer[2 * 512];

void setup() {
  adc.begin(A0, 100000);                  // pin, conversions per second (the fastest step at or below)
  adc.startContinuous(buffer, 512);       // DMA fills the first half, the second, the first ...
}

void loop() {
  if (const uint16_t *full = adc.readBuffer()) {   // a full half, yours until the next readBuffer()
    // full[0] .. full[511]
  }
}
```

## Where it runs

On the families whose ADC1 DMA request is DMA1 channel 1 (ch32-device-data dma.csv): CH32V003 / V006, V103, V20x, V205, V30x, L103 / M103, M030, X033 / X035, and the ADC1 pads of X305 / X315. On V407 / V467 it is DMA1 channel 12, used the same way (checked on a V407 at 200 MHz). On H415 / H416 / H417 the DMAMUX routes it to DMA1 channel 1 (checked on an H417).

## How it works

- **`begin()`: the ADC runs free.** In continuous mode each conversion starts the next. No timer is used.
- **The rate comes in steps.** The ADC clock and one of eight sample times set the conversion rate. Each step's rate differs between families and no table gives it, so `begin()` runs every step once, measures it, and takes the fastest at or below the rate asked for. `sampleRate()` is that measured value.
- **Nothing is slower than the slowest step.** For example about 47,600 conversions per second on a CH32V307 at 96 MHz, 250,000 on a CH32X035, 99,000 on a CH32V407 at 200 MHz and 198,000 on a CH32H417 (measured 2026-10-27). Ask for less and you get the slowest step; for a slow steady rate, use `beginTimer()`.
- **`beginTimer()`: one round per tick of a timer.** The core takes a free timer among the ones the family's ADC can be started by (TIM3's TRGO on the F1-shaped families, TIM2's or TIM1's TRGO on V00x / X035, a channel's compare where there is no TRGO, as on M030; `triggerTimer()` says which), through the same timer manager as `analogWrite()` and `timerBegin()`, so it fails when none is free. The rate is the timer clock divided by whole numbers: exact where it divides, and as slow as you like (below the free-running floor, below 1 Hz). The sample time is the longest whose round still fits in 80 % of a tick (measured once per step in `beginTimer()`); `sampleRate()` is the timer's rate.
- **`beginExternal()`: one round per edge of a pin** (`RISING`, `FALLING` or `CHANGE`). The pin is the family's ADC trigger pad: any pad of bit 11 on most families (PA11, PB11, PC11 ...), bit 12 on X305 / X315, PD3 or PC2 on V00x, PA14 or PB6 on M030 (`isExternalTriggerPin()`). V00x's pad starts one round per pulse on `CHANGE`, so `CHANGE` is refused there (`isExternalTriggerPin(pin, CHANGE)` is false). The edge goes through the pin's EXTI line as an event; an `attachInterrupt()` on the same pin still works and shares the edge setting. `maxRate` (edges per second expected) picks the sample time as for a timer; without it the longest. `sampleRate()` is 0.
- **`conversions()`** counts the rounds done since `startContinuous()` - round n is at `buffer[(n * pins) % (2 * samples)]` - so a sketch sees each triggered round land without waiting for a half to fill.
- **One to six pins.** Several are converted in turn; the buffer holds pin 0, pin 1, ... pin 0, pin 1, ..., and `sampleRate()` counts rounds. A half has to be a multiple of the pin count.
- **Values are the converter's own** (0 .. 4095 on 12-bit families, 0 .. 1023 on 10-bit ones), without `analogRead()`'s scaling (the same values `analogRead()` reads at the converter's own width; `analogReadCalibrated()` adds L103 / M103's offset correction).

## API

| Call | Meaning |
|---|---|
| `begin(pin, rate)` / `begin(pins, count, rate)` | Free-running. False when a pin is no ADC1 input. Call again to change pins or rate |
| `beginTimer(pin, rate)` / `beginTimer(pins, count, rate)` | One round per timer tick at `rate` a second. False when a pin is no ADC1 input, no trigger timer is free, or the rate is 0 or out of the timers' reach |
| `beginExternal(pin, triggerPin, edge[, maxRate])` / `beginExternal(pins, count, triggerPin, edge[, maxRate])` | One round per edge of `triggerPin`. False when it is no trigger pad |
| `isExternalTriggerPin(pin)` | Whether the pin can be `beginExternal()`'s trigger on this part |
| `conversions()` / `triggerTimer()` | Rounds since `startContinuous()`; the timer `beginTimer()` took (its number) |
| `startContinuous(buffer, samples)` | `buffer` holds 2 x `samples`; the halves are filled in turn |
| `startContinuous(buf0, buf1, samples)` | CH32RVHsAdc's form; buf1 has to follow buf0 directly (two halves of one array) |
| `readBuffer()` | The full half, or nullptr. The sketch's until the next call |
| `overruns()` | Halves the DMA came back to before the sketch took them |
| `buffersFilled()` / `sampleRate()` / `sampleTimeStep()` | Halves filled, the chosen step's rate (measured), the step (0 fastest) |
| `stop()` / `end()` | Stop; `end()` also frees the timer or the trigger pin and gives the ADC back to `analogRead()` |

Do not use `analogRead()` while a capture runs; after `end()` it works again. There is one ADC1: a stream and arduino-esp32's `analogContinuous()` (in the core, Arduino.h) do not run at once.

## Examples

- [AdcStreamStats](examples/AdcStreamStats/README.md): capture one pin and print, each second, the count, the minimum, maximum and mean, and any lost halves.
- [AdcTriggered](examples/AdcTriggered/README.md): convert a pin on a timer's tick, then on another pin's rising edge.
