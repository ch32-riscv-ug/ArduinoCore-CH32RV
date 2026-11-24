# CH32RVLowPowerTimer

[日本語](README.ja.md)

The low-power timer (LPTIM): one on CH32L103 / M103, two on CH32H41x. A 16-bit counter that keeps running through `lightSleep()` (Stop) on LSI or LSE and wakes the chip, or counts a pin's pulses with every clock off. No arduino-esp32 counterpart.

```cpp
#include <CH32RV.h>
#include <CH32RVLowPowerTimer.h>

CH32RVLowPowerTimer lpt;                    // LPTIM1 (H41x's LPTIM2: lpt2(2))

void setup() {
  lpt.begin(CH32RVLowPowerTimerClock::Lsi, 32);   // count LSI / 32
  lpt.setPeriod(lpt.clockHz() / 4);         // every 250 ms
  lpt.onPeriod(tick);                       // called in the timer's interrupt
  lpt.start();
  CH32RV.enableExtiLineWakeup(lpt.wakeLine());   // lightSleep() ends at each period
}
```

| Function | Meaning |
|---|---|
| `begin(clock, divider)` | Counts an internal clock: `CH32RVLowPowerTimerClock::Bus` (PCLK1; stops in Stop), `CH32RVLowPowerTimerClock::Hsi`, `CH32RVLowPowerTimerClock::Lse` (only when it runs: the RTC Clock menu's LSE), `CH32RVLowPowerTimerClock::Lsi` (started here; nominal, off by tens of percent). divider 1, 2, 4 .. 128 |
| `beginCounter(pin, edge, asynchronous, clock)` | Counts the edges of the CH1 pad (and starts). By default sampled by an internal clock (no edge lost, up to that clock's rate); asynchronous makes the pin the timer's clock, counting with every clock off, but up to the first five edges after start() are not counted (RM 17.3.9; 2-3 on H417). CHANGE (both edges) only when sampled |
| `beginEncoder(pinA, pinB, clock)` | A quadrature encoder on CH1 / CH2 (every edge of both, four counts a cycle), up and down between 0 and period - 1; clock at least 4x the edge rate |
| `setPeriod(ticks)` | The counter goes back to 0 every ticks (1 .. 65536; 65536 by default) |
| `setCompare(ticks)` | Where onCompare() and the output's edge come (below the period; until set, the period's last count) |
| `output(pin, inverted)` | The timer's output on its OUT pad: high from the compare value to the end of the period (low with inverted); a PWM after start(), one pulse after startOnce() |
| `start()` / `startOnce()` / `stop()` | Over and over / one period / stopped and back to 0 |
| `count()` | The value now (the timer's clock is not the bus's: read until two reads agree) |
| `direction()` | The encoder's direction now: 1 up, -1 down |
| `onPeriod(cb)` / `onCompare(cb)` | Called in the timer's interrupt at the end of each period / at the compare value; the interrupt (and the wake-up) is on only while one is set |
| `clockHz()` | Ticks a second (begin()'s clock / divider; 0 for an asynchronous counter) |
| `wakeLine()` | The EXTI line for `CH32RV.enableExtiLineWakeup()`: lightSleep() ends at the period's end and at the compare match (the hardware's wake-up comes from both, whatever onPeriod() / onCompare() say) |
| `end()` | Stops and lets the timer go |

- The pads are the variant's `CH32RV_LPTIMn_PADS`. H41x chooses the function per pad, so any of them; on L103 they have to share one AFIO route (PB12-PB15, or PB5-PB7 / PB2).
- ARR and CMP writes travel to the timer's clock, and the next write waits until it arrives (ARROK / CMPOK, RM 17.3.8): on a slow clock such as LSI / 128 setPeriod() can take a few ms. An asynchronous counter has no clock until its pin moves, so the wait gives up after 5 ms.
- One object per timer.
- Quirks measured on CH32H417 (2026-11-23): CFGR's CLKMX_SEL (the clock) takes only when it changes while the timer is on (written while off, or the same value again once on, it stays PCLK1); the wake-up reaches its EXTI line only with CFGR's TIMOUT set, and comes at a compare match too (its interrupt on or not). The library handles both.

## Examples

- [LowPowerTick](examples/LowPowerTick/README.md): sleeps in lightSleep() and lets the LPTIM on LSI wake the chip every 250 ms.

## Tests

periph::test_lptim: the probe's PWM on a labelled line that is an LPTIM's CH1 pad (the DUT finds it: lptfind); the periods on the bus clock, LSI's rate, the sampled and asynchronous counts (1 kHz, 10 kHz) and waking from lightSleep() at an LSI period, judged from what the DUT counts. Passing on H417 (LPTIM2 on PB10; LSI about 42.3 kHz, a 200 ms period slept 189 ms; 2026-11-23). L103 built only (no bench line is an LPTIM pad there).
