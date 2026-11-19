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

## Analog watchdog

```cpp
adc.setWatchdog(0, 1000);          // a flag when a value leaves 0..1000 (-1: every pin, 0..: that pin only)
if (adc.watchdogTripped()) { ... } // read and clear the flag
adc.clearWatchdog();
```

The ADC compares every conversion in hardware: nothing for the sketch to do and nothing missed. Values are the
converter's units, as in the buffer. No interrupt (the flag is read). Linked only into a sketch that calls
`setWatchdog()`. Seen on V203, V003 and H417 with the probe holding the line low and high: the flag rises exactly
when the line is outside the window (2026-11-18, periph::test_adc_watchdog).

## Both ADCs together (dual mode)

```cpp
const pin_size_t v[] = {PA1}, i[] = {PA2};
adc.beginTimer(v, 1, 10000);       // ADC1: PA1 at 10 kHz
adc.simultaneous(i, 1);            // ADC2: PA2 at the same moment
adc.startContinuous(buf, 256);     // buf: ADC1's PA1, ADC2's PA2, ADC1's PA1, ...

adc.beginInterleaved(PA4);         // ADC2 and ADC1 in turn on PA4: twice one ADC's rate
```

On the series with an ADC2 (V30x, V4x7, X305 / X315, H41x; the variant's `CH32RV_ADC_DUALMOD_SHIFT`).
`simultaneous()` comes after `begin()`, `beginTimer()` or `beginExternal()`: ADC2 converts `pins2[i]` on the same
trigger as ADC1's pin i. A round holds ADC1's pin 0, ADC2's pin 0, ADC1's pin 1, ...; `sampleRate()` counts rounds.
The two cannot convert one channel at once (the manual forbids it); on X305 / X315 ADC2 has its own pads only.
`beginInterleaved()` converts one pad in turn at the shortest sample time, and `readBuffer()`'s half comes in time
order (false on X305 / X315: no pad reaches both ADCs). The watchdog sees ADC1's values only. Injected, alternate
trigger and slow interleaved modes are not there yet. Checked on V407 and H417 reading the two DAC outputs
(2026-11-19, analog::test_adc_dual; interleaved about 3.57 M samples a second on V407, 7.16 M on H417). Linked only
into a sketch that calls `simultaneous()` or `beginInterleaved()`.

## Injected group (CH32RVAdcInjected)

```cpp
#include <CH32RVAdcInjected.h>

CH32RVAdcInjected inj;
const pin_size_t pins[] = {PA0, PA1};
inj.begin(pins, 2);                    // 1 .. 4 pins, converted in this order as one round
uint16_t v[2];
inj.convert(v);                        // one round now (software start), waited for
inj.onRound([](const uint16_t *v, uint8_t n) { /* from the ADC's interrupt */ });
inj.startTimer(1000);                  // rounds 1000 times a second from a timer
```

ADC1's injected group: up to four channels converted at a set instant, between `analogRead()`'s and the stream's
regular conversions (a motor's currents at a set point of the PWM period, say). Values are the converter's raw
counts (`bits()` bits; `analogReadResolution()` does not apply). `startTimer()` takes a free timer among the
sources the chip can start the group with (the variant's `CH32RV_ADC_JTRIGGERS`, generated from device-data's
adc_triggers.csv: a timer's TRGO or a channel's compare); `rate()` says the rate it gives. `value(i)` and
`rounds()` give the last values and the count. Checked on V203 and X035: the values with the probe holding the line
low / high, and 499 rounds in 500 ms at 1 kHz (2026-11-19, periph::test_adc_injected).

## Examples

- [AdcStreamStats](examples/AdcStreamStats/README.md): capture one pin and print, each second, the count, the minimum, maximum and mean, and any lost halves.
- [AdcTriggered](examples/AdcTriggered/README.md): convert a pin on a timer's tick, then on another pin's rising edge.
