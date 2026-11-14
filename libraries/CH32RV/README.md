# CH32RV

Three things live here: the **`CH32RV` object** for the chip as a whole, the
**examples for the core's own APIs**, and the **register-level escape hatch**.

## The `CH32RV` object (CH32RVSystem.h)

The role of `ESP.*` in the ESP32 core: what concerns the whole chip and has no
standard Arduino API.

```cpp
#include <CH32RV.h>

CH32RV.restart();                        // software reset (does not return)
CH32RV.resetReason();                    // CH32RV_RESET_SOFTWARE / _WATCHDOG / _PANIC (Stack Check reset on a stack overflow) ...
CH32RV.resetReasonName();                // "software" and so on, for printing
CH32RV.wdtEnable(2000);                  // reset unless fed every 2 s
CH32RV.wdtFeed();                        // call it in loop()
CH32RV.clockSource();                    // CH32RV_CLOCK_SOURCE_HSE / _HSI (Clock Source menu)
CH32RV.getVddMillivolts();               // supply voltage (mV), from the ADC's internal reference
CH32RV.getHeapSize();                    // the heap, and what malloc() can still hand out
CH32RV.getFreeHeap();
CH32RV.getStackSize();                   // the stack reserved (Stack Size menu) and what was left at its deepest
CH32RV.getMinFreeStack();                // (the latter with the Stack Check menu on; 0 when off)
CH32RV.getChipModel();                   // "CH32V003": the series built for
CH32RV.getSketchSize();                  // flash used, and what is left of the part's
CH32RV.getFreeSketchSpace();
CH32RV.getFlashChipSize();               // the silicon's flash, from its signature
CH32RV.getUniqueId();                    // 64 bits of the chip's unique ID
analogReadTemp();                        // die temperature (°C), NAN without a sensor (core)
analogReadVref();                        // the internal reference, on analogRead()'s scale (core)
```

- **There is no `wdtDisable()`.** Once running, the IWDG cannot be stopped,
  so a `disable()` that did not would be worse than none.
- **The watchdog can run from power-on.** Tools → Watchdog → `Running from power-on`
  has the core rewrite the option byte (USER's IWDG_SW) at boot, and the IWDG
  then runs from power-on at its default period (about 128 ms on V003).
  Set the period with `wdtEnable()` early in `setup()` and call `wdtFeed()` in
  `loop()`. The factory entry does nothing and leaves the chip as set. To go
  back, upload a sketch built with the factory entry, run
  `ch32rv target option set iwdg=sw --yes` and reset (a `Running from power-on`
  sketch left on the chip writes USER back to hw at every boot). The menu is on
  V00x, M007, X033/X035, X305/X315, V20x, V205, V30x, L103/M103 and V407/V467.
- **The timeout is approximate.** The LSI it counts can be off by close to
  ±50 % by the datasheet; the conversion errs towards a timeout shorter than
  asked for.
- `wdtEnable()` **returns false** on CH32M030, which has no IWDG. CH32X033/X035
  have no LSI; their IWDG runs on HSI/1024 (46.875 kHz).
- **`clockSource()`** says HSE only when the Clock Source menu chose the crystal
  and it started. A crystal that does not start leaves the same CPU Clock on HSI
  (`F_CPU` unchanged, at HSI's accuracy).
- **Voltage and temperature are computed from typical values.** The internal
  reference (Vrefint) is the datasheet's typical value, within a few percent.
  The temperature is uncalibrated and can be tens of °C off on some series
  (V103's datasheet gives V25 as 1.1-1.6 V). The sensor is on V103, V20x, V205,
  V30x, V407/V467 and L103/M103. M030 has no reference data and returns 0.
- **`getUniqueId()`** is UNIID1 | UNIID2 << 32: only these 64 of the 96 UID bits
  differ from chip to chip. It is not called `getEfuseMac()` because it is not a
  MAC address.
- **H41x** has `setVio18Millivolts()` / `getVio18Millivolts()` for the supply
  of the high-speed pins (1200, 1800, 2500 or 3300 mV); the VIO18 Supply menu
  sets it before `setup()` instead.

### Sleep (`lightSleep()` / `deepSleep()`)

ESP32's `esp_sleep_*` meanings, under `CH32RV` names.

```cpp
CH32RV.enableTimerWakeup(5 * 1000000ULL);  // wake in 5 s
CH32RV.enablePinWakeup(PA1, LOW);          // or when PA1 reads LOW (set the pin's mode yourself)
CH32RV.lightSleep();                       // returns on waking; RAM and pin states kept
CH32RV.deepSleep();                        // starts again from setup() (a software restart)
CH32RV.wakeupCause();                      // CH32RV_WAKEUP_TIMER / _PIN / _UNDEFINED
CH32RV.timeDomain();                       // moves on whenever unmeasured time was skipped
```

- **Supported families.**
  - V003, V00x and X033/X035, woken by the AWU.
  - V20x, V205, V30x, L103/M103 and H41x, woken by the RTC alarm; they sleep in stop. H41x's stop takes both cores: the V3F waits in it while the V5F runs the sketch (and with the V3F build, the V5F is never started); its regulator stays in normal mode. Its pin wakeup is not yet checked on a board.
  - Elsewhere the calls return `CH32RV_SLEEP_UNSUPPORTED`.
- **On the RTC families `time()` is the RTC.**
  - It runs at 1 Hz from LSI, measured once, and the time survives resets and standby. After a power cut it starts again from 1970.
  - `settimeofday()` sets it, including through SNTP's `configTime()`.
  - BKP DR1-DR4 are the core's. H41x has no BKP data registers: the core keeps them in RAM, which a reset keeps, so after a power cut with a VBAT battery its RTC starts again too.
- **Nothing sleeps by itself.** Only these calls do. `Serial` finishes sending first.
- **`millis()` counts the sleep.** The AWU measures the time asleep, and the core adds it.
  - On the AWU families a pin wake part-way through cannot be measured. It adds nothing and moves `timeDomain()` on instead. The RTC families measure it with the RTC.
  - Accuracy (2026-10-28, one chip of each): V003 matched the wall clock within 0.1 %, X035 slept 3 % long, and V006 slept 10 % short. On those two the AWU runs at a different rate asleep than when it is measured awake.
- **The watchdog looks paused while asleep.** The IWDG cannot be stopped, so the core works around it:
  - just before sleeping it stretches the IWDG to its longest;
  - it wakes every half of that to feed it, and nothing else runs;
  - on waking it puts back the `wdtEnable()` setting.
- **`deepSleep()`** enters real standby (RAM lost, wakes through a reset) on the RTC families but H41x (which has no standby) when only the timer wakes it and the watchdog is off. Otherwise it sleeps like `lightSleep()` and then restarts. `wakeupCause()` reads the cause across the restart.
- **Size.** 0 B unless used. On V003, timer sleep adds about 1.4 KB, and pin wakeups about 0.65 KB more.

The design and the measurements are in sections 4.3 and 5.1 of
[the design of the A features](../../docs/peripheral-a-design.ja.md) (Japanese).

The object, the examples and the escape hatch share a library because an
Arduino platform can only ship examples through one, and a library needs at
least one header to be well formed. Rather than the empty placeholder other
cores use for this, the header does a job.

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
#include <internal/ch32rv_registers.h>

CH32RV_TIM_ATRLR(CH32RV_TIM2_BASE) = 999;   // straight at the timer
```

The register map (`internal/ch32rv_registers.h`), the GPIO helpers (`internal/ch32rv_gpio.h`), the
pin encoding (`internal/ch32rv_pins.h`) and the alternate-function route tables
(`internal/ch32rv_route.h`) are included by name; `CH32RV.h` does not bring them in. `internal/`
says what they are. Two
warnings belong on the tin:

1. **This layer is not kept from release to release.** The names are this core's own,
   not a vendor SDK's, and they can change between releases. What a release keeps is in
   [the public API](../../docs/public-api.ja.md).
2. **The core is using these registers too.** Writing `AFIO_PCFR1` by hand
   while `Serial` or `Wire` is open fights `begin()` and `setRoute()`, and the
   symptom is a peripheral that moves pins the next time it is opened.
