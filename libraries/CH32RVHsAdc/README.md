# CH32RVHsAdc

[日本語](README.ja.md)

The CH32H415 / H416 / H417 high-speed ADC (HSADC): 10 bits, up to 20 Msps.

| Pins | Channel | Supply |
|---|---|---|
| PC0, PC1, PC2, PC3 | 0-3 | VIO18 |
| PF8, PF9, PF10 | 4-6 | VDDIO (not bonded out on H415) |

```cpp
#include <CH32RVHsAdc.h>

CH32RVHsAdc hsadc;
hsadc.begin(PF8, 20000000);     // pin, samples per second
uint16_t v = hsadc.read();      // one conversion, 0..1023

static uint16_t a[4096], b[4096];
hsadc.startContinuous(a, b, 4096);                 // DMA fills a, b, a ...
if (const uint16_t *full = hsadc.readBuffer()) {   // from loop()
    // full[0..4095]
}
```

## Rate

- The HSADC clock is PLL_CLK (400 MHz) / (divider + 1), at most 100 MHz.
  A conversion takes 5 of those clocks.
- The rates run from 20 Msps down to 1.25 Msps in steps. `begin()` takes the
  nearest rate at or below the one asked for, and `sampleRate()` says which.
- Measured on CH32H417 (2026-10-21): 20.0 Msps continuous with nothing lost,
  and 1022-1023 with the pad at 3.3 V.

## Input range (important)

- The input range is 0 V up to the pin's own supply.
- **PC0-PC3 are VIO18 pins.** On the evaluation board VIO18 starts at 1.2 V,
  and those inputs are rated to VIO18 + 0.3 V.
- Raise VIO18 to 3.3 V before putting 0-3.3 V on them: use **Tools > VIO18
  Supply**, or call `CH32RV.setVio18Millivolts(3300)`. Do not raise it on a
  board that has a 1.8 V part on VIO18.
- PF8-PF10 are VDDIO (3.3 V) pins and need none of this.

## Continuous capture

- The two buffers alternate. A buffer `readBuffer()` returns is the sketch's
  until the next `readBuffer()` call.
- Finish with it within one buffer time (samples / rate: 819 us for 16384
  samples at 20 Msps). After that the DMA comes back to it.
- A buffer filled again before the sketch took it counts in `overruns()`, and
  its data is lost.
- Keep the buffers in RAM the DMA reaches: ordinary globals or statics, not
  the V5F's TCMs.
- `HsAdcStream` streams 20 Msps x 2 bytes = 40 MB/s over SuperSpeed with the
  vendor class's `writeDirect()`. Read it with several libusb async transfers
  queued. One synchronous read at a time drops buffers between reads.
