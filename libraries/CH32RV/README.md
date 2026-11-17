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
CH32RV.wwdgEnable(20000, 10000);         // window watchdog: feed 10-20 ms after the last feed
CH32RV.wwdgFeed();
CH32RV.setBackupRegister(0, 42);         // 16-bit words that survive a reset (RTC families)
CH32RV.getBackupRegister(0);
CH32RV.setAlarm(time(nullptr) + 60, onAlarm);  // the RTC alarm, in time()'s seconds or a struct tm
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
CH32RV.crc32(buf, len);                  // CRC-32/MPEG-2, on the CRC block where there is one
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
- **The window watchdog (WWDG)** is on every family. `wwdgEnable(timeoutUs, windowUs, earlyWarning)`:
  the chip resets unless `wwdgFeed()` comes within `timeoutUs` of the last feed, and (with a window) also
  when a feed comes before `windowUs` has passed. It counts PCLK1 / 4096 / 1-8 with 63 counts, so the
  longest timeout is short - `wwdgMaxTimeoutUs()`, about 43 ms at 48 MHz, 258 ms at 8 MHz - and
  `wwdgEnable()` returns false (starting nothing) beyond it. Both edges are exact to one count: the reset
  never comes after the timeout, and a feed is always allowed from `windowUs` on. `earlyWarning` runs in
  an interrupt one count before the reset: feed there, or record why. Like the IWDG it cannot be stopped.
  `resetReason()` says `CH32RV_RESET_WINDOW_WATCHDOG` for its reset, `CH32RV_RESET_WATCHDOG` for the
  IWDG's. Sleeps do not feed it: its clock stops in stop / standby, but the plain-WFI parts of a sleep
  (under a second on the RTC families, under 150 ms on M030) count.
- **Backup registers** are 16-bit words that survive a reset and standby: `backupRegisterCount()`,
  `getBackupRegister(i)`, `setBackupRegister(i, v)`, STM32duino's names, indexed from 0. They are BKP DR5
  and up (DR1-DR4 hold the RTC's settings): 6 words on V103, V205 and L103/M103, 6 or 38 on V20x, V30x
  and V4x7 (the part is asked whether DR42 exists), kept through a power cut by a VBAT battery. H41x and
  X305/X315 have no BKP data registers: there they are 16 words of RAM that no startup clears, with a
  check word - kept through every reset, zero again after power-up (`backupRegistersKeepPower()` says
  false). V003, V00x, M030 and X033/X035 have none (the count is 0).
- **The RTC alarm** (the RTC families). The calendar is POSIX's, as on arduino-esp32: `time()`,
  `localtime_r()`, `mktime()`, `settimeofday()`. `setAlarm(when, callback)` takes `time()`'s seconds or a
  `struct tm` in local time; the callback runs once, in the alarm's interrupt, within a second after
  `when` (the RTC counts whole seconds). One alarm: a new call replaces it, `clearAlarm()` drops it,
  `getAlarm()` says what is still to come (0 once it has run). `settimeofday()` keeps it at its wall time.
  `setAlarm()` returns false where there is no RTC. Why this shape: arduino-esp32 has no RTC alarm API
  (its calendar is POSIX time), and RTCZero's per-field setters and match masks fit a calendar RTC, not
  this one - a 32-bit seconds counter with one compare register. An absolute time in the units `time()`
  already uses is that register as it is; a repeating alarm is one `setAlarm()` in the callback.
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
CH32RV.enableAlarmWakeup();                // or at setAlarm()'s time (RTC families)
CH32RV.wakeupCause();                      // CH32RV_WAKEUP_TIMER / _PIN / _ALARM / _UNDEFINED
CH32RV.timeDomain();                       // moves on whenever unmeasured time was skipped
```

- **Supported families.**
  - V003, V00x, M030 and X033/X035, woken by the AWU. M030's AWU counts LSI / 51000, about 150 ms a count (no prescaler): a remainder under that is slept in a plain WFI, and the first sleep of a boot is up to 300 ms longer while it measures the LSI.
  - V20x, V205, V30x, V4x7, L103/M103, X305/X315 and H41x, woken by the RTC alarm; they sleep in stop. H41x's stop takes both cores: the V3F waits in it while the V5F runs the sketch (and with the V3F build, the V5F is never started); its regulator stays in normal mode, as does X305/X315's. Its pin wakeup is not yet checked on a board.
  - V103 does not sleep yet (`CH32RV_SLEEP_UNSUPPORTED`): with its alarm fixed (2026-11-15) the alarm, `setAlarm()` and the backup registers work, but its first `lightSleep(200 ms)` still never returned from stop, and neither the probe nor NRST reached it (2026-11-17, TODO). H41x, V20x, V205, V30x and V407 were run; X305/X315 and M030 are built and not yet run.
  - `CH32RV_SLEEP_UNSUPPORTED` remains for a family added later without its sleep.
- **On the RTC families `time()` is the RTC.**
  - It runs at 1 Hz from LSI, measured once, and the time survives resets and standby. After a power cut it starts again from 1970.
  - `settimeofday()` sets it, including through SNTP's `configTime()`.
  - BKP DR1-DR4 are the core's. H41x and X305/X315 have no BKP data registers: the core keeps them in RAM, which a reset keeps, so after a power cut with a VBAT battery their RTC starts again too. X305/X315 has no backup domain at all (its RTC's clock bits are in the RTC itself).
- **Nothing sleeps by itself.** Only these calls do. `Serial` finishes sending first.
- **`millis()` counts the sleep.** The AWU measures the time asleep, and the core adds it.
  - On the AWU families a pin wake part-way through cannot be measured. It adds nothing and moves `timeDomain()` on instead. The RTC families measure it with the RTC.
  - Accuracy (2026-10-28, one chip of each): V003 matched the wall clock within 0.1 %, X035 slept 3 % long, and V006 slept 10 % short. On those two the AWU runs at a different rate asleep than when it is measured awake.
- **The watchdog looks paused while asleep.** The IWDG cannot be stopped, so the core works around it:
  - just before sleeping it stretches the IWDG to its longest;
  - it wakes every half of that to feed it, and nothing else runs;
  - on waking it puts back the `wdtEnable()` setting.
- **`deepSleep()`** enters real standby (RAM lost, wakes through a reset) on the RTC families but H41x and X305/X315 (which have no standby) when only the timer or the alarm wakes it and the watchdog is off; `wakeupCause()` then says `CH32RV_WAKEUP_ALARM` when the alarm came first (the backup domain carries that), and the alarm's callback does not run (RAM was lost). Otherwise it sleeps like `lightSleep()` and then restarts. `wakeupCause()` reads the cause across the restart.
- **Size.** 0 B unless used. On V003, timer sleep adds about 1.4 KB, and pin wakeups about 0.65 KB more.

The design and the measurements are in sections 4.3 and 5.1 of
[the design of the A features](../../docs/peripheral-a-design.ja.md) (Japanese).

### Utilities: CRC, random words, millivolts, RGB LEDs, interrupt priorities

```cpp
CH32RV.crc32(buf, len);                  // CRC-32/MPEG-2; ch32rv_crc32() in ch32rv_crc.h
ch32rv_random();                         // arduino-esp32's esp_random(): a 32-bit random word
ch32rv_fill_random(buf, len);            // and esp_fill_random()
analogReadMilliVolts(A1);                // arduino-esp32's: the pad's voltage in mV
rgbLedWrite(PA1, r, g, b);               // arduino-esp32's: one WS2812 / SK6812 LED on any pad
ch32rv_rgb_led_write(PA1, grb, n * 3);   // a chain of them in one frame
ch32rv_irq_set_priority(irqn, CH32RV_IRQ_PRIORITY(1));   // ch32rv_priority.h
```

- **CRC.** `CH32RV.crc32(data, len[, crc])` is the CRC block's CRC-32: polynomial 0x04C11DB7, from 0xFFFFFFFF,
  most significant bit first, no final XOR - the catalogue's CRC-32/MPEG-2 (`0x0376E6E7` for "123456789"; EVT
  CRC_Calculation's 32 words give `0x199AC3CA`). It is not zlib's / Ethernet's CRC-32 (`0xCBF43926`). Pass the
  last result as `crc` to go on with more data; any length and alignment. The block computes whole words on V103,
  V20x, V205, V30x, V4x7, L103 / M103, X305 / X315 and H41x (`CH32RV_HAS_CRC_HARDWARE`), 64 words at a time with
  interrupts masked so a handler may use it too; the tail and the other series go through a 16-entry table, with
  the same results. On H41x one core at a time. About 140 bytes on V003 when used.
- **Random words.** `ch32rv_random()` / `ch32rv_fill_random(buf, len)` are arduino-esp32's `esp_random()` /
  `esp_fill_random()` under this core's names (no `esp_` symbols here). On V30x, V4x7 and H41x (`CH32RV_RNG_BASE`)
  they come from the hardware generator, four of its raw words folded into each one (its raw words repeated
  and leaned to byte patterns on V307 and H417; `ch32rv_rng_read()` stays raw for Mbed TLS, which conditions it
  itself). Elsewhere - and if the generator ever reports an error that does not clear -
  they are a software mix of the chip's unique ID and the SysTick count at each call: different from chip to chip
  and run to run, but predictable, **not cryptographic** (`ch32rv_rng_read()` is the call that says false instead).
  `random()` / `randomSeed()` are left as Arduino defines them: without `randomSeed()` the sequence is the same every
  run, and `randomSeed(n)` repeats it - the Arduino reference documents both, and the series would otherwise differ
  in behaviour by whether they have a generator. arduino-esp32 instead feeds `random()` from the hardware until
  `randomSeed()` is called; here `randomSeed(ch32rv_random())` gets a different sequence each run on every series.
- **`analogReadMilliVolts(pin)`** (arduino-esp32's name). **The supply is measured on every call**: right before
  the pad is converted, the ADC converts its internal reference (Vrefint), and mV = Vrefint's typical mV x the
  pad's reading / Vrefint's reading - the same as working VDD out from Vrefint and scaling the reading by it, so
  VDD is never assumed. The only exception is M030, which has no Vrefint in the data: there VDD is taken as
  3300 mV. The reading carries the family's offset correction, as `analogReadCalibrated()`.
  - **Time per call: four conversions** (Vrefint twice and the pad twice: the first after a channel switch is
    thrown away) where `analogRead()` does one, each at the longest sample time - about 250 ADC clocks, so about
    85 us per call at a 12 MHz ADC clock (V20x / V30x at 96 MHz), 4 x `analogRead()` in general. CH32V103 adds
    20 us: its reference is switched on for each call and off after it (with it on, the pads read wrong there).
    About 300 bytes more than `analogRead()` on V003.
  - **Accuracy is not measured yet**: the error is Vrefint's spread around its typical (the datasheets' min /
    max, a few percent) plus the converter's offset and gain; the automatic test only checks a driven pad within
    80-110 % of the supply at 1 and under 10 % at 0. The characteristic is for the manual test with an outside
    voltage source.
- **`rgbLedWrite(pin, r, g, b)`** sends one WS2812 / SK6812-type LED's 24 bits (GRB) on any pad and returns;
  `rgbLedWriteOrdered(pin, LED_COLOR_ORDER_RGB, ...)` takes another order, `neopixelWrite()` is the deprecated old
  name - arduino-esp32's three. **A chain is `ch32rv_rgb_led_write(pin, bytes, len)`**: every LED's bytes, in the
  order they go out (GRB for WS2812B, four bytes for RGBW parts), in one frame with interrupts off for all of it
  (about 30 us an LED). It is this core's own (arduino-esp32 has no chain call; the shape is Adafruit_NeoPixel's
  `show()`), because separate `rgbLedWrite()` calls leave the line low between LEDs for the call itself: 2-5 us
  on most boards, 13 us on V006 at 24 MHz (bench, 2026-11-17), where the LEDs latched there and the chain broke.
  The targets are T0H 375 ns, T1H 800 ns and a 1.25 us bit; from 72 MHz up every edge is timed on SysTick,
  below that by counted instructions from RAM (at 8 MHz the bit is 1.625 us: the loop itself is longer; below
  36 MHz the last bit of each byte, which loads the next one, is up to 4 cycles longer). Neither call waits for
  the latch: the line must then stay low for the LED's reset time (50 us on WS2812, 280 us on WS2812B-V5 and
  many newer parts). Measured 2026-11-17 (WireSkein's rgb_led, 60 MS/s, one LED): T0H 333-433 ns, T1H
  767-900 ns, the bit 1.22-1.43 us on V003, V006, V103, V203, V205, X035 and H417. The ARGB hardware of V4x7 /
  X3x5 is a later, faster path.
- **Interrupt priorities** (`#include <ch32rv_priority.h>`): `ch32rv_irq_set_priority(irqn, priority)` /
  `ch32rv_irq_get_priority(irqn)` on the PFIC's byte, smaller more urgent, as WCH's `NVIC_SetPriority()`.
  `CH32RV_IRQ_PRIORITY(level)` is the byte of a preemption level, 0 to `CH32RV_IRQ_PREEMPT_LEVELS - 1`: 2 levels on
  V003, V00x, V103, V20x, V205, V4x7, X035, L103 / M103, X305 / X315 and H41x's V3F; 4 on V30x and M030; 8 on H41x's
  V5F (the startup's INTSYSCR, the EVTs' core_riscv.h). A more urgent level interrupts a less urgent handler, as
  deep as the hardware register stack allows (V30x: 4 levels, 3 deep). Interrupt numbers: `CH32RV_IRQN_<name>`
  (the EVTs' names), a timer capability's `update_irq`, `ch32rv_pin_irqn(pin)` (the pin's EXTI vector, shared by
  its group), `CH32RV_IRQN_CORE_SYSTICK`. **The contract with the core's interrupts:** the core never writes a
  priority, so every interrupt - SysTick (millis()), the USARTs, the timers of tone() / Servo, EXTI, USB, the RTC
  alarm, the WWDG's early warning - stays at 0, the most urgent level, and nothing nests until a sketch sets
  something. A handler set less urgent is interrupted by the core's (millis() and Serial keep running through
  it): the use the core is built for. The core's own may be made less urgent too; its thread-side code guards what
  it shares with its handlers by masking every interrupt, which holds at any level, but no driver is re-entrant -
  a handler that can interrupt a driver's handler must not call that driver. `noInterrupts()` masks every level.
  On H41x each core has its own. V103 (no HPE) nests by the PFIC as the EVT says; not yet checked on the board.

### H41x's two cores (V5F and V3F)

With CPU Core = V5F (the default) the sketch runs on the V5F and the V3F is a second core (`setup1()` / `loop1()`,
`CH32RV.startV3F()`, example [DualCoreV3F](examples/DualCoreV3F/README.md)). Exclusion and notification between the
two, called the same way from either core (the C functions are in `ch32rv_multicore.h`):

```cpp
CH32RV.hsemTake(0);                 // take hardware semaphore (HSEM) channel 0, waiting for it
shared.a = 1; shared.b = 2;          // what the two cores share
CH32RV.hsemRelease(0);              // give it back
if (CH32RV.hsemTryTake(1)) { ... }  // try without waiting

CH32RV.ipcOnReceive(onMessage);     // onMessage(uint32_t) runs in an interrupt for each message to this core
CH32RV.ipcSend(42);                 // to the other core (waits while its last message has not been taken)
uint32_t m; CH32RV.ipcReceive(m);   // take one without a callback
```

- **HSEM** (`hsemCount()` = 31; channel 31 is the core's): only the core that took a channel can release it -
  `hsemRelease()` of the other core's returns false and leaves it taken. A channel belongs to a core: an interrupt
  handler cannot take what its own core's loop holds (`hsemTryTake()` is false; never wait for it there).
  `hsemTake(ch, ms)` gives up after `ms` (no limit by default) and returns false at once for a channel this core
  holds.
- **IPC**: one 32-bit message at a time in each direction (the IPC's channel 0 to the V5F, 1 to the V3F; 2 and 3
  are left to the sketch). `ipcSend(msg, ms)` waits up to `ms` for the last one to be taken (no limit by default; 0
  in an interrupt handler) and returns whether it went in. The callback runs in the IPC interrupt of the core that
  set it (IPC_CH0 on the V5F, IPC_CH1 on the V3F). A message waits for a core that is not running yet. With CPU
  Core = V3F the V5F stays asleep: the three IPC calls return false (the HSEM works as ever).
- **What the V3F can use:** a function handed to the V3F runs with interrupts on, and an interrupt enabled from the
  V3F is taken there (the low 32 are each core's own, a shared one moves to the core that enables it, RM 4.7.5.45):
  `Serial1` and the like begun on the V3F, `pinMode()` / `digitalWrite()`, `attachInterrupt()`, a timer's callback,
  `millis()` / `delay()`. Priorities are the V3F's two levels (bit 7).
- **The rules:** a peripheral (a USART, timer, SPI, I2C, ADC, ...) is used from one core. The V5F's `Serial` (and
  `printf`, and the core's console) is the V5F's; the V3F takes another (`Serial1`, ...). A pin and an EXTI vector
  group (EXTI9_5, EXTI15_10) belong to one core too. The registers several drivers share - RCC's clock enables, a
  port's configuration, AFIO, EXTI - are written by the core under a lock (HSEM channel 31), so the pins of one port
  may be split between the cores.
- **Not on the V3F:** the system services (USB, the IP stack, USB PD), sleep, `restart()`, the watchdogs, the RTC
  and the flash (`CH32RV.*`), anything timed by counting instructions (`rgbLedWrite()`: `F_CPU` is the V5F's 400
  MHz), and the heap (`malloc`, `new`, `String`: picolibc's malloc has no lock).
- Example: [DualCoreMessages](examples/DualCoreMessages/README.md). Tests: selfapi's `test_cores_hsem`,
  `test_cores_ipc` and `test_cores_v3f_serial` (one image holds both cores' code, so one write).

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
| [AnalogRead](examples/AnalogRead/README.md) | `analogRead`, `analogReadMilliVolts` |
| [AnalogContinuous](examples/AnalogContinuous/README.md) | `analogContinuous`, `analogContinuousRead` (arduino-esp32's continuous ADC API) |
| [Fade](examples/Fade/README.md) | `analogWrite` |
| [ToneMelody](examples/ToneMelody/README.md) | `tone`, `noTone` |
| [PinInterrupt](examples/PinInterrupt/README.md) | `attachInterrupt`, sharing state with an ISR |
| [ShiftOut](examples/ShiftOut/README.md) | `shiftOut` into a 74HC595 |
| [PulseIn](examples/PulseIn/README.md) | `pulseIn`, `delayMicroseconds` |
| [TimerAlarm](examples/TimerAlarm/README.md) | `timerBegin`, `timerAttachInterrupt`, `timerAlarm` (arduino-esp32's timer API) |
| [Timing](examples/Timing/README.md) | `millis`, `micros`, and how they wrap |
| [RandomNumbers](examples/RandomNumbers/README.md) | `random`, `randomSeed`, `ch32rv_random`, `ch32rv_fill_random` |
| [Crc32](examples/Crc32/README.md) | `CH32RV.crc32`, in one call and in pieces |
| [RgbLed](examples/RgbLed/README.md) | `rgbLedWrite` for one LED, `ch32rv_rgb_led_write` for a chain |
| [InterruptPriority](examples/InterruptPriority/README.md) | `ch32rv_irq_set_priority`, nesting under the core's SysTick |
| [AnalogResolution](examples/AnalogResolution/README.md) | `analogReadResolution`, `analogWriteResolution` |
| [CriticalSection](examples/CriticalSection/README.md) | `interrupts`, `noInterrupts`, `volatile` |
| [PrintFormatting](examples/PrintFormatting/README.md) | `Print`, `String`, `dtostrf` |
| [TimerResource](examples/TimerResource/README.md) | Advanced timer acquisition, setup and ownership |
| [PinCapabilities](examples/PinCapabilities/README.md) | which pads this chip actually has |
| [DualCoreV3F](examples/DualCoreV3F/README.md) | CH32H417: `setup1()` / `loop1()` and `CH32RV.startV3F()` on the second core |
| [DualCoreMessages](examples/DualCoreMessages/README.md) | CH32H417: `CH32RV.ipcSend()` and `CH32RV.hsemTake()` between the cores, `Serial1` on the V3F |

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
