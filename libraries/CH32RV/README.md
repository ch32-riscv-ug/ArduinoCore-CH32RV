# CH32RV

Two things live here: the **examples for the core's own APIs**, and the
**register-level escape hatch**.

They share a library because an Arduino platform can only ship examples through
one, and a library needs at least one header to be well formed. Rather than the
empty placeholder other cores use for this, the header does a job.

## The examples

See [basic example setup](examples/README.md) and each example README for wiring, operation, API roles and adaptation notes.

`digitalWrite()`, `analogRead()`, `tone()` and the rest belong to the core, not
to any library, so their examples are here. Every one of them:

- names its pins as pads (`PC0`, `PA1`) or through the names the variant
  provides (`A0`, `SDA`, `SCK`…), never as bare numbers. A pad an example
  picks is the example's own choice, meant to be changed - a Generic board is
  a silicon series, not a PCB, so it defines no `LED_BUILTIN` and the core
  will not invent one (`docs/board-layer-rules.ja.md`);
- says in its header comment what it shows and what wiring it needs;
- is compiled in CI for the widest board and the narrowest one.

| Example | Shows |
|---|---|
| [Blink](examples/Blink/README.md) | `pinMode`, `digitalWrite`, `delay` |
| [SerialEcho](examples/SerialEcho/README.md) | `Serial` in and out |
| [AnalogRead](examples/AnalogRead/README.md) | `analogRead` |
| [Fade](examples/Fade/README.md) | `analogWrite` |
| [ToneMelody](examples/ToneMelody/README.md) | `tone`, `noTone` |
| [PinInterrupt](examples/PinInterrupt/README.md) | `attachInterrupt`, sharing state with an ISR |
| [ShiftOut](examples/ShiftOut/README.md) | `shiftOut` into a 74HC595 |
| [PulseIn](examples/PulseIn/README.md) | `pulseIn`, `delayMicroseconds` |
| [Timing](examples/Timing/README.md) | `millis`, `micros`, and how they wrap |
| [RandomNumbers](examples/RandomNumbers/README.md) | `random`, `randomSeed` |
| [AnalogResolution](examples/AnalogResolution/README.md) | `analogReadResolution`, `analogWriteResolution` |
| [CriticalSection](examples/CriticalSection/README.md) | `interrupts`, `noInterrupts`, `volatile` |
| [PrintFormatting](examples/PrintFormatting/README.md) | `Print`, `String`, `dtostrf` |
| [TimerResource](examples/TimerResource/README.md) | Advanced timer acquisition, setup and ownership |
| [PinCapabilities](examples/PinCapabilities/README.md) | which pads this chip actually has |

**PinCapabilities is the one to run first on a board you do not know.** CH32 pin
numbers are `((port + 2) << 5) | bit`, so they are sparse: PA0 is 64, PB0 is 96,
and most numbers in between belong to no pad (0..63 are kept for a product
board's printed numbers). That sketch asks the variant instead
of guessing.

### Product board templates

One per product board: every peripheral the board brings out, set up with all
its pins named, to start from and cut down. What cannot run together with the
rest is commented out, saying what it would cost. Each is built for its own
board (fqbn) only (`board:`, `docs/examples-build-rules.ja.md`).

| Example | Board | Sets up |
|---|---|---|
| [BoardWeActCH32X035](examples/BoardWeActCH32X035/README.md) | WeAct CH32X035 Core Board (CH32X035F8U6) | USB (`SerialUSB` + the DFU runtime for uploads), USB PD (5 V), two UARTs, SPI, two analog inputs, PWM, the LED (PB12). I2C exists only on the USB or SWD pins, so it is commented out |
| [BoardUIAPduinoProMicro](examples/BoardUIAPduinoProMicro/README.md) | UIAPduino Pro Micro CH32V003 V1.4 | UART, I2C (a bus scan), SPI, three analog inputs, PWM, the LED (PC0). SPI stays AUTO in the board menu; the maker's NSS pads (PC1 / PC0) are I2C's SDA and the LED, so CS is PC3, with the alternatives in comments |

Both were run on the boards (2026-10-14).

## The escape hatch

```cpp
#include <CH32RV.h>

CH32RV_TIM_ATRLR(CH32RV_TIM2_BASE) = 999;   // straight at the timer
```

`CH32RV.h` pulls in the register map, the GPIO helpers, the pin encoding and the
alternate-function route tables. Two warnings belong on the tin:

1. **This layer is not stable.** The names are this core's own, not a vendor
   SDK's, and they will change - the register map is on its way to being
   generated from `ch32-device-data` rather than hand-written. A sketch that
   uses it is pinned to a core version in a way that a sketch using the Arduino
   API is not.
2. **The core is using these registers too.** Writing `AFIO_PCFR1` by hand
   while `Serial` or `Wire` is open fights `begin()` and `setRoute()`, and the
   symptom is a peripheral that moves pins the next time it is opened.
