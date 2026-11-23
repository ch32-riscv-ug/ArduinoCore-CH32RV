# CH32RVDfsdm

[日本語](README.ja.md)

The DFSDM of CH32H41x (digital filter for sigma-delta modulators): a 1-bit stream - a PDM microphone, an isolated sigma-delta modulator - turned into samples by a sinc filter. No Arduino or arduino-esp32 counterpart (arduino-esp32's PDM microphones go through its I2S peripheral).

```cpp
#include <CH32RVDfsdm.h>

CH32RVDfsdm mic;                                   // filter 0 (CH32RVDfsdm(1): filter 1)

void setup() {
  mic.begin(PE9, PD6, 2000000, 64);                // CKOUT clocks the microphone, data on DATIN1; 2 MHz / 64
}

void loop() {
  int32_t s = mic.read();                          // 31.25 kHz samples, -fullScale() .. fullScale()
}
```

| Function | Meaning |
|---|---|
| `begin(clockPin, dataPin, clockHz, oversample = 64, order = 3, integrator = 1)` | The clock pin: the CKOUT pad (the DFSDM clocks the modulator at HCLK / 2 .. 256, the nearest at or under clockHz) or the channel's CKIN pad (the modulator's own clock; clockHz is what sampleRate() counts from). The data pin: DATIN0 or DATIN1, which picks the channel |
| `read()` / `read(buf, n)` | The next sample (waits) / up to n samples now in the ring |
| `available()` | Samples in the ring |
| `clock()` / `sampleRate()` | CKOUT's rate / clock / (oversample x integrator) |
| `fullScale()` | A stream of ones reads +fullScale(), zeros -fullScale() |
| `overruns()` | Samples lost: the ring full, or the filter's own overrun |
| `setBuffer(n)` / `setFallingEdge(on)` | Before `begin()`: the ring's size (64 by default); data on SCK's falling edge |
| `setWatchdog(low, high)` / `watchdog()` | The analog watchdog on this filter's samples: one above high / below low sets bit 1 / bit 0 of `watchdog()`, which clears them as it reads. Any time |
| `maximum()` / `minimum()` | The largest / smallest sample since the last call of each |
| `setShortCircuit(bits)` / `shortCircuit()` | Before `begin()`: that many equal bits in a row (1 .. 255; 0 off) on the input set `shortCircuit()` (an input stuck at a rail) |
| `end()` | Stops; the pads back to inputs |

- The filter: sinc order 1 .. 5 (0: FastSinc), oversampling 1 .. 1024, then the integrator summing 1 .. 256 results. fullScale() is oversample ^ order x integrator (FastSinc: 2 x oversample ^ 2 x integrator), shifted right to stay in 24 bits; begin() is false when it passes the filter's 32 bits.
- Filter 0 and 1 each take one channel and can run together: on two data pads, or on one (the second filter reads it through the other channel - two microphones on one line, the second with `setFallingEdge(true)`). They share CKOUT: the second begin() must ask the same rate. The channels' settings take a write only with the whole DFSDM off, so the second begin() stops the first filter for a moment (a few of its samples are lost while it settles again).
- Samples arrive by the filter's interrupt (DFSDM0 / DFSDM1, attached at run time) into a ring of int32_t on the heap.
- Pads: the variant's `CH32RV_DFSDM_PADS`. The detectors are polled (no callbacks yet). Not covered: clock-absence detection, the watchdog's own fast filter, the break outputs to the timers, injected conversions and their triggers, parallel input from the CPU or the ADC, DMA.

## Examples

- [PdmLevel](examples/PdmLevel/README.md): a PDM microphone's RMS and peak.

## Tests

periph::test_dfsdm: CKOUT (2 MHz, judged by WireSkein) and DATIN, the probe's PWM making the stream - held high +64^3 and held low -64^3 exactly (sinc3, oversampling 64), a 10 kHz square of duty 1/4 and 3/4 a mean of -1/2 and +1/2 of it, the samples at sampleRate() with no overrun; filter 1 alone; both filters on the one line (rising and falling edge); and the probe's 1 MHz on CKIN as the clock. Passes on H417 (PE9 / PD6 / PD7, 2026-11-23). periph::test_dfsdm_detectors: held high the watchdog at +-1/2 full scale sets its high bit (wide, nothing), the extremes both +full and a short circuit at 200 bits; held low the low bit, -full, a short circuit; a 10 kHz square no short circuit (H417, 2026-11-23). No microphone connected; H415 / H416 built only.
