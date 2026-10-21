# CH32RVLogicCapture

[日本語](README.ja.md)

Sample a whole GPIO port (16 inputs) into RAM by DMA: the front end of a
logic analyzer. CH32H415 / H416 / H417.

```cpp
#include <CH32RVLogicCapture.h>

CH32RVLogicCapture logic;
static uint16_t buf[32768];
logic.begin(PORT_E, 10000000);              // port, samples per second
logic.startContinuous(buf, 32768);          // halves of 16384, in turn
if (const uint16_t *half = logic.readBuffer()) { ... }   // from loop()

uint32_t rate = logic.captureBurst(buf, 32768);   // one fast burst
```

Each sample is the port's input register (bit n = Pxn), whatever the pins'
modes.

## Two ways (measured on CH32H417 at HCLK 100 MHz, 2026-10-21)

| | Continuous (`startContinuous()`) | Burst (`captureBurst()`) |
|---|---|---|
| How | TIM6 update requests DMA2 channel 8 | The DMA copies back to back, no timer |
| Rate | Up to F_CPU / 11 = 9.09 MS/s | 5 clocks a sample = 20 MS/s |
| Timing | Set by the timer, exact | Set by the DMA; bus contention shifts it |
| Length | Endless, in a circular buffer | Up to 65535 samples |

**Continuous**
- A DMA transfer from a port takes 8 HCLK clocks, and a request arriving
  during one is dropped, so one channel stops at 12.5 MS/s (F_CPU / 8).
- With the USB streaming at the same time, samples are lost below that too
  when the buffer shares a 128 KB SRAM bank with the USB's transfers (151 ppm
  at 9.09 MS/s with the buffer in one bank). `begin()` clamps to F_CPU / 11
  for now; the buffer placement and a two-channel 20 MS/s mode are being
  worked out (measurements: docs/h417-capture.ja.md, in Japanese).

**Burst**
- The interval is the DMA's speed, not a timer's. When the CPU or another DMA
  uses the bus at the same time, a sample comes a clock late. In 16-bit, 6 of
  about 320 PWM periods were off by a few samples.
- Interrupts are off while it runs.

## Other notes

- TIM6 is taken through the core's timer manager (`CH32RVTimer.h`), so
  `analogWrite()`, `tone()` and Servo leave it alone, and it leaves them
  alone.
- A continuous half is the sketch's until the next `readBuffer()`. Finish
  with it before the DMA comes back: half the samples / the rate. A half
  filled again before the sketch took it counts in `overruns()`, and its data
  is lost.
- Keep the buffer in RAM the DMA reaches (globals and statics).
- `LogicStream` streams 9.09 MS/s x 2 bytes = 18 MB/s over SuperSpeed.
- Inputs go up to the port's supply: PE7-PE15 and others are VIO18 pins (see
  "Input range" in the CH32RVHsAdc README).
