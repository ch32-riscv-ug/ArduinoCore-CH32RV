# CH32RVAdcStream

[日本語](README.ja.md)

Keeps ADC1 converting at a steady rate and moves the results by DMA into two buffers in turn. Instead of calling `analogRead()` over and over, the conversions never stop and the sketch takes each full half. The API has the shape of [CH32RVHsAdc](../CH32RVHsAdc/README.md), the H41x's high-speed ADC library.

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

- **The ADC runs free.** In continuous mode each conversion starts the next. No timer is used.
- **The rate comes in steps.** The ADC clock and one of eight sample times set the conversion rate. Each step's rate differs between families and no table gives it, so `begin()` runs every step once, measures it, and takes the fastest at or below the rate asked for. `sampleRate()` is that measured value.
- **Nothing is slower than the slowest step.** For example about 47,600 conversions per second on a CH32V307 at 96 MHz, 250,000 on a CH32X035 99,000 on a CH32V407 at 200 MHz and 198,000 on a CH32H417 (measured 2026-10-27). Ask for less and you get the slowest step. For a slow steady rate, take every n-th value or call `analogRead()` from a timer.
- **One to six pins.** Several are converted in turn; the buffer holds pin 0, pin 1, ... pin 0, pin 1, ..., and `sampleRate()` counts rounds. A half has to be a multiple of the pin count.
- **Values are the converter's own** (0 .. 4095 on 12-bit families, 0 .. 1023 on 10-bit ones), without `analogRead()`'s scaling or calibration offset.

## API

| Call | Meaning |
|---|---|
| `begin(pin, rate)` / `begin(pins, count, rate)` | False when a pin is no ADC1 input. Call again to change pins or rate |
| `startContinuous(buffer, samples)` | `buffer` holds 2 x `samples`; the halves are filled in turn |
| `startContinuous(buf0, buf1, samples)` | CH32RVHsAdc's form; buf1 has to follow buf0 directly (two halves of one array) |
| `readBuffer()` | The full half, or nullptr. The sketch's until the next call |
| `overruns()` | Halves the DMA came back to before the sketch took them |
| `buffersFilled()` / `sampleRate()` / `sampleTimeStep()` | Halves filled, the chosen step's rate (measured), the step (0 fastest) |
| `stop()` / `end()` | Stop; `end()` also gives the ADC back to `analogRead()` |

Do not use `analogRead()` while a capture runs; after `end()` it works again.

## Examples

- [AdcStreamStats](examples/AdcStreamStats/README.md): capture one pin and print, each second, the count, the minimum, maximum and mean, and any lost halves.
