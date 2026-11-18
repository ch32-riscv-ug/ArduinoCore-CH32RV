# CH32RVTimerOutput

[日本語](README.ja.md)

Timer outputs beyond `analogWrite()`, in six classes:

- **`CH32RVComplementaryPwm`** drives a channel's output and its complementary output (CHx and CHxN of an advanced timer) as a half bridge's two sides, with a dead time and a break input.
- **`CH32RVOnePulse`** makes one pulse of a set width after a set delay (one-pulse mode), started by the sketch or in hardware by an edge.
- **`CH32RVOutputCompare`** makes a pad go active, inactive or toggle at a set time in each period (output compare), with a callback at the match.
- **`CH32RVPwmTimer`** runs several channels of one timer (and their CHxN) as PWM on one period, edge- or center-aligned.
- **`CH32RVPhaseShift`** puts square waves (50 % duty) of one frequency on a timer's channels, each at its own phase.
- **`CH32RVSixStep`** drives a three-phase bridge in six steps (trapezoidal) on an advanced timer's CH1 .. CH3 and CH1N .. CH3N.

```cpp
#include <CH32RVTimerOutput.h>

CH32RVComplementaryPwm bridge;
CH32RVOnePulse shot;

void setup() {
  bridge.setDeadTime(500);              // ns
  bridge.begin(PA8, PB13, 20000);       // CH1 and CH1N of TIM1, 20 kHz
  bridge.attachBreak(PB12);             // BKIN to GND: both off at once
  bridge.write(128);                    // of 256 (8 bits)
  shot.begin(PA6);
}

void loop() {
  shot.fire(100, 20);                   // 20 us high, 100 us from now
  delay(10);
}
```

## Pins and timers

- **Any route.** A timer's pads on any route the chip offers: the default pads, the pads a remap moves the timer to, and on V205, X305/X315 and H41x each pad's alternate function (the variant's `CH32RV_TIMER_PADS`; `ch32rv_timer_pad_find()` in [CH32RVTimer.h](../../cores/arduino/CH32RVTimer.h)). A pad on several timers takes the first free one. On the families with one AFIO field per timer, a pad on another route moves all of that timer's pads.
- **The timer is borrowed whole** ([timer resource contract](../../docs/timer-design.ja.md)), politely: `begin()` is false when `analogWrite()`, `tone()`, Servo or another library has it. An object whose timer is taken over later stops (`isAttached()` false). `end()` leaves its pads plain low outputs.

## CH32RVComplementaryPwm

| Call | Meaning |
|---|---|
| `begin(pin, pinN, frequency, resolution = 8)` | `pin` is CHx and `pinN` the CHxN of the same channel and route of an advanced timer (TIM1, TIM8 ...). `resolution`: the bits `write()` takes (1 to 16). |
| `write(duty)` | CHx high for `duty / 2^resolution` of each period, less the dead time; CHxN the rest, less the dead time. 0: CHx low, CHxN high; `2^resolution`: the reverse. |
| `setDeadTime(ns)` / `deadTime()` | Both low around each switch. Rounded up to the timer's dead-time steps (its clock; coarser for long times, up to 1008 clocks: 10.5 µs at 96 MHz). Returns what was set. Before or after `begin()`. |
| `attachBreak(pin, activeLevel = LOW, mode = INPUT_PULLUP)` | The timer's break input (BKIN, on the same route). At `activeLevel` the hardware turns both outputs off (low) at once, without the CPU. |
| `breakActive()` / `resume()` | A break came (latched). `resume()` turns the outputs back on once the break input is inactive; false while it is still active. |
| `detachBreak()`, `end()`, `isAttached()` | |

Both outputs are active high. On a break and after `end()` both are low.

## CH32RVOnePulse

| Call | Meaning |
|---|---|
| `begin(pin, polarity = HIGH, triggerPin = -1, triggerEdge = RISING)` | `pin`: the pulse's pad (a channel of a timer). `polarity`: `HIGH` pulses from a low line, `LOW` the reverse. `triggerPin`: a pad of channel 1 or 2 of the same timer (another channel than the output's), whose edge starts the pulse in hardware; -1 for `fire()` only. |
| `setPulse(delayMicros, widthMicros)` | The pulse: `delayMicros` after the start (one tick at least), `widthMicros` long. The tick is the timer clock divided so that both fit the counter. |
| `fire()` / `fire(delayMicros, widthMicros)` | Start one now. False while one is under way. |
| `busy()`, `end()`, `isAttached()` | |

## CH32RVOutputCompare

| Call | Meaning |
|---|---|
| `begin(pin, periodMicros)` | The counter runs over `periodMicros` on `pin`'s timer and channel. |
| `set(mode, atMicros)` | `atMicros` into each period, the pad does `mode`: `ACTIVE` (high), `INACTIVE` (low), `TOGGLE`, or at once `FORCE_ACTIVE` / `FORCE_INACTIVE`; `TIMING` leaves the pad alone. `TOGGLE` makes a square wave of half the period's frequency. |
| `onMatch(callback)` | Called from the timer's interrupt at each match; `nullptr` for none. |
| `tickHz()`, `end()`, `isAttached()` | |

## CH32RVPwmTimer

| Call | What it does |
|---|---|
| `begin(pin, frequency, resolution = 8, align = EDGE)` | starts on `pin`'s (CHx or CHxN) timer and route. `CENTER`: the counter runs up and down, every channel's pulse centred on the same instant |
| `attach(pin)` | another pad of the same timer and route (at duty 0) |
| `write(pin, duty)` | `duty / 2^resolution` high. A CHxN pad is its channel inverted, so this sets the channel |
| `startWith(master)` | this timer starts when `master` does (master's TRGO on its counter's enable, this one in trigger mode); both counters restart from 0 and their periods start together. False where the chip joins the two timers by no internal trigger |
| `startOn(pin, edge = RISING)` | the counter stops and waits for an edge on `pin` (this timer's channel 1 or 2 pad, not one of its outputs), then runs from 0 |
| `timer()`, `end()`, `isAttached()` | |

Which timer can follow which (the ITR table) comes from each family's reference manual, `tools/generate/timer/itr.csv`, as the variant's `CH32RV_TIMER_ITR`; the core's `ch32rv_timer_trigger_from(slave, master)` looks it up.

## CH32RVPhaseShift

| Call | What it does |
|---|---|
| `begin(pin, frequency, degrees = 0)` | a square wave on `pin`'s channel (the counter at twice the frequency, the output toggling at the compare) |
| `attach(pin, degrees)` | another channel of the same timer and route, `degrees` behind; the outputs restart together |
| `setPhase(pin, degrees)` | a new phase; the outputs restart together (up to a period disturbed) |
| `end()`, `isAttached()` | |

## CH32RVSixStep

| Call | What it does |
|---|---|
| `begin(uHigh, uLow, vHigh, vLow, wHigh, wLow, frequency, resolution = 8)` | U, V, W high sides (CH1 .. CH3) and low sides (CH1N .. CH3N) of one advanced timer and route; all off to begin with |
| `write(duty)` | the high side's PWM duty |
| `step(n)` | step 0 .. 5, all three phases at once (the commutation event); 6 or more: all off |
| `setDeadTime(ns)`, `currentStep()`, `end()`, `isAttached()` | |

Steps: 0 = U PWM and V's low side on, 1 = U and W, 2 = V and W, 3 = V and U, 4 = W and U, 5 = W and V. The third phase has both sides low.

## Status

Built for every series (2026-11-17). tests/instrumented/signals checks them on the probe's capture: `test_complementary` (the dead time with WireSkein's dead_time at 100 MS/s, the break where a line reaches BKIN), `test_one_pulse`, `test_output_compare`, `test_pwm_timer` (edge-aligned: the rises together; center-aligned: 75 % and 25 % 90 degrees apart) and `test_phase_shift` (90 and 270 degrees), `test_timer_sync` (two timers' rises together) and `test_timer_trigger` (the output starting at the probe's edge). `test_six_step` runs where six lines reach the bridge's pads, which no bench has yet: on V203 the DUT read its six pads back and found each step as the table says (2026-11-19). Not yet built: the asymmetric dead time (M030, H417).

## Examples

- [HalfBridge](examples/HalfBridge/README.md): CHx / CHxN with a dead time and a break input.
- [OnePulse](examples/OnePulse/README.md): single pulses, and a square wave by output compare.
