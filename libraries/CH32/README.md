# CH32

Two things live here: the **examples for the core's own APIs**, and the
**register-level escape hatch**.

They share a library because an Arduino platform can only ship examples through
one, and a library needs at least one header to be well formed. Rather than the
empty placeholder other cores use for this, the header does a job.

## The examples

`digitalWrite()`, `analogRead()`, `tone()` and the rest belong to the core, not
to any library, so their examples are here. Every one of them:

- takes its pins from names the variant provides (`LED_BUILTIN`, `A0`, `SDA`,
  `SCK`…) rather than numbers, so it runs on any board;
- says in its header comment what it shows and what wiring it needs;
- is compiled in CI for the widest board and the narrowest one.

| Example | Shows |
|---|---|
| Blink | `pinMode`, `digitalWrite`, `delay` |
| SerialEcho | `Serial` in and out |
| AnalogRead | `analogRead` |
| Fade | `analogWrite` |
| ToneMelody | `tone`, `noTone` |
| PinInterrupt | `attachInterrupt`, sharing state with an ISR |
| ShiftOut | `shiftOut` into a 74HC595 |
| PulseIn | `pulseIn`, `delayMicroseconds` |
| Timing | `millis`, `micros`, and how they wrap |
| RandomNumbers | `random`, `randomSeed` |
| AnalogResolution | `analogReadResolution`, `analogWriteResolution` |
| CriticalSection | `interrupts`, `noInterrupts`, `volatile` |
| PrintFormatting | `Print`, `String`, `dtostrf` |
| PinCapabilities | which pads this chip actually has |

**PinCapabilities is the one to run first on a board you do not know.** CH32 pin
numbers are `(port << 5) | bit`, so they are sparse: PA0 is 0, PB0 is 32, and
most numbers in between belong to no pad. That sketch asks the variant instead
of guessing.

## The escape hatch

```cpp
#include <CH32.h>

CH32_TIM_ATRLR(CH32_TIM2_BASE) = 999;   // straight at the timer
```

`CH32.h` pulls in the register map, the GPIO helpers, the pin encoding and the
alternate-function route tables. Two warnings belong on the tin:

1. **This layer is not stable.** The names are this core's own, not a vendor
   SDK's, and they will change - the register map is on its way to being
   generated from `ch32-device-data` rather than hand-written. A sketch that
   uses it is pinned to a core version in a way that a sketch using the Arduino
   API is not.
2. **The core is using these registers too.** Writing `AFIO_PCFR1` by hand
   while `Serial` or `Wire` is open fights `begin()` and `setRoute()`, and the
   symptom is a peripheral that moves pins the next time it is opened.
