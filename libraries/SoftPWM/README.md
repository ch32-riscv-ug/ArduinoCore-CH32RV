# SoftPWM

[日本語](README.ja.md)

PWM on any pin, by software, with the API of the well-known SoftPWM library for AVR (Brett Hagman's).

```cpp
#include <SoftPWM.h>

void setup() {
  SoftPWMBegin();
  SoftPWMSet(PA0, 64);                    // 64 / 255 high
  SoftPWMSetFadeTime(PA1, 500, 1000);     // ms from 0 to 255 and from 255 to 0
  SoftPWMSet(PA1, 255);                   // fades up over 500 ms
}
```

| Function | What it does |
|---|---|
| `SoftPWMBegin(polarity = SOFTPWM_NORMAL)` | takes a timer (the highest-numbered free one, as `timerBegin()`); false when none is free |
| `SoftPWMSet(pin, value, hardset = 0)` | `value / 255` high (inverted: low); the pin becomes an output. `hardset`: at once, past a fade |
| `SoftPWMSetPercent(pin, percent, hardset = 0)` | the same in 0 .. 100 % |
| `SoftPWMSetFadeTime(pin, up, down)` | ms for 0 to 255 and for 255 to 0; 0: at once |
| `SoftPWMSetPolarity(pin, polarity)` | `SOFTPWM_INVERTED`: low is active |
| `SoftPWMEnd(pin)` | stops it at its inactive level; `ALL` stops every pin and gives the timer back |

- `SOFTPWM_FREQ` (60 Hz by default), 256 steps, up to `SOFTPWM_MAXCHANNELS` pins (16).
- The interrupt comes `SOFTPWM_FREQ x 256` times a second (15.4 kHz at 60 Hz); the edges move by its latency (a few us, more while other interrupts run).
- Differences from the original: pins are `int`, not `int8_t` (PC0 and on are 128 and up on this core); `SoftPWMBegin()` returns a bool.
- Where the hardware PWM (`analogWrite()`) reaches the pin, it is exact and costs no CPU.

## Test

signals::test_soft_pwm: 25 % and 75 % on two lines, 60 Hz and the duty judged by WireSkein.
