# CH32RVLogicCapture

[日本語](README.ja.md)

Sample a whole GPIO port (16 inputs) into RAM by DMA: the front end of a
logic analyzer. Every family; run on CH32H415 / H416 / H417 (both ways, at
speed), and continuous capture on V003 / V103 / V205 / V307 / X035 / X315
(LogicPulseDecode and the bench's PWM checks, 2026-10-28); built on the others.

| Series | Timer | DMA |
|---|---|---|
| CH32H415-H417 | TIM6 | DMA2 channel 8 (DMAMUX request 122) |
| CH32V303 / V305 / V307 / V317, V407 / V467 | TIM6 | DMA2 channel 3 |
| CH32V103, V203 / V208, V205, L103 / M103, X305 / X315 | TIM4 | DMA1 channel 7 |
| CH32V002-V007, M007, V003, X033 / X035 | TIM2 | DMA1 channel 2 |
| CH32M030 | TIM3 | DMA1 channel 3 |

- The 24-pin ports (X033 / X035, X305 / X315) are sampled on their low 16 pins.
- On the small parts whose TIM2 also drives PWM and `tone()`, `begin()` fails
  while those hold it.
- The rate limit (`maxContinuousRate()`, F_CPU / 11) was measured on the
  H417 only.

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

## Fast capture on the UHSIF data lines (CH32H415 / H416 / H417)

On H41x, naming the 16 UHSIF data lines instead of a port captures them
continuously at up to 200 MS/s, with no CPU and no timer. It is built on
[CH32RVUhsif](../CH32RVUhsif/README.md).

```cpp
static uint16_t buf[32768] __attribute__((aligned(32)));
logic.begin(UHSIF_DATA, 200000000);         // 200, 160, 133, 120, 100 ... 6.25 MS/s
logic.startContinuous(buf, 32768);          // halves of 16384, in turn
if (const uint16_t *half = logic.readBuffer()) { ... }
```

| Data line | Pin |
|---|---|
| DATA0-5 | PD10-PD15 |
| DATA6-8 | PF0-PF2 |
| DATA9-12 | PC6-PC9 |
| DATA13-15 | PA13-PA15 |

- Bit n of each sample is DATAn, whatever the pins' modes.
- The rate is SYSCLK (400 MHz) / n or the USBHS PLL (480 MHz) / n;
  `begin()` takes the nearest (`sampleRate()` says which). `maxUhsifRate()`
  is 200 MS/s.
  - The USBHS PLL comes from the crystal, so its timing is exact; SYSCLK
    comes from the internal oscillator and can be about 1% off (one PWM
    captured from both gave periods 1% apart).
- No sample is lost. Two threads write the two halves in turn, so while one
  moves on to its next buffer the other is writing. Read back whole after a
  stop, a 390.625 kHz PWM kept its period across every boundary and the wrap
  at 6.25, 100 and 200 MS/s (CH32H417, 2026-11-11).
- `startContinuous()` takes 64-65472 samples, a multiple of 64, in a
  32-byte aligned buffer.
- Make each half last 20 us or more (8000 samples or more at 200 MS/s):
  each half is an interrupt, and short ones leave the CPU nothing else.
- `captureBurst()` is not offered (returns 0).
- Streamed to a PC over USB SuperSpeed (a ring of 8 x 32 KB, each run of
  filled parts handed to `writeDirect()`), nothing was lost in 30 seconds at
  up to 133 MS/s (266 MB/s). At 160 MS/s a few parts were lost every few
  seconds, and at 200 MS/s (400 MB/s) the USB topped out near 365 MB/s and
  about a tenth was lost. The USB work runs on the 100 MHz V3F
  (2026-11-11).

## Other notes

- The timer is taken through the core's timer manager (`CH32RVTimer.h`), so
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

## Detailed example guides

- [LogicBurst](examples/LogicBurst/README.md): connections, operation, API use and adaptation notes.
- [LogicStream](examples/LogicStream/README.md): connections, operation, API use and adaptation notes.
- [LogicUhsif](examples/LogicUhsif/README.md): capture the UHSIF data lines at 200 MS/s and measure a PWM's frequency and duty cycle (H41x).
- [LogicPulseDecode](examples/LogicPulseDecode/README.md): frequency and duty cycle of a signal, on every family (its own PWM with no wiring).
