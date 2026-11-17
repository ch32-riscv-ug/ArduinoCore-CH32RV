# CH32RVTimerInput

[日本語](README.ja.md)

Timers as inputs, in three classes:

- **`CH32RVPulseCapture`** measures a signal's period and high time (pulse width).
- **`CH32RVEncoder`** counts a quadrature rotary encoder.
- **`CH32RVInputCapture`** gives each edge of a signal with its time.

In the first two the timer does the counting and the sketch reads the result when it wants to. No interrupt calls into the sketch, and the hardware keeps counting whether or not anything is read. `CH32RVInputCapture` keeps every edge: its interrupt queues them, or hands each to a callback.

```cpp
#include <CH32RVTimerInput.h>

CH32RVPulseCapture pulse;
CH32RVEncoder knob;
CH32RVInputCapture edges;

void setup() {
  Serial.begin(115200);
  pulse.begin(PA6);                    // a channel 1 or 2 pad of a timer
  knob.attachFullQuad(PB6, PB7);       // channels 1 and 2 of one timer
  edges.begin(PB0, CHANGE);            // any channel of a timer
}

void loop() {
  CH32RVPulseCapture::Pulse p;
  if (pulse.read(p)) {                 // the newest whole period since the last read()
    Serial.println(p.frequencyHz());
  }
  Serial.println((long)knob.getCount());
  CH32RVInputCapture::Edge e;
  while (edges.read(e)) {              // every edge, oldest first
    Serial.println((unsigned long)e.ticks);   // microseconds by default
  }
}
```

## Pins and timers

- **Usable pins.** A timer channel's pad on any route the chip offers: the default pads, the pads a remap moves the timer to, and on V205, X305/X315 and H41x each pad's alternate function (the variant's `CH32RV_TIMER_PADS`, `ch32rv_timer_pad_find()` in [CH32RVTimer.h](../../cores/arduino/CH32RVTimer.h)). `CH32RVPulseCapture` and `CH32RVEncoder` need channel 1 or 2 (the slave reset and the encoder mode are wired to those two); `CH32RVInputCapture` takes any channel. A pad on several timers takes the first one that is free.
- **Other pins.** `begin()` / `attach*()` return false. An encoder's A and B must be channels 1 and 2 of the same timer, on the same route.
- **A remap moves the whole timer.** On the families with one AFIO field per timer, connecting a pad on another route moves all of that timer's pads.
- **The timer is borrowed whole** ([timer resource contract](../../docs/timer-design.ja.md)). If `analogWrite()`, `tone()` or Servo already holds it, the call returns false.
- **When another user takes the timer later.** If `analogWrite()` or another user takes the same timer afterwards, this object stops: `read()` returns false, `getCount()` keeps its last value and `isAttached()` is false.
- **Clock and low-power modes.** Timer ticks are the timer's clock (`ch32rv_timer_clock_hz()`) divided by the prescaler each class picks. In stop and standby the timer stops too, and edges in between are not counted.

## CH32RVPulseCapture

The timer runs in PWM-input mode. Each rising edge resets the counter and captures the period, and each falling edge captures the high time. The hardware refreshes both every period.

| Call | Meaning |
|---|---|
| `begin(pin, minHz = 10, mode = INPUT)` | `minHz` is the slowest signal to measure, and it sets the tick (the resolution). For example, a 16-bit timer at 48 MHz ticks every 1.5 µs for 10 Hz, and every 21 ns for 1 kHz. `mode` is `INPUT`, `INPUT_PULLUP` or `INPUT_PULLDOWN`. |
| `read(pulse)` | The newest whole period since the last `read()`, or false if there is none. The first period after `begin()` is dropped, and so is the first after a stall, because the counter did not start on their rising edge. |
| `stalled()` | No rising edge for `1 / minHz`. Either the line is held (`digitalRead()` tells high from low) or the signal is slower than `minHz`. It stays true until `read()` next returns a period. |
| `tickHz()` | Ticks per second. |
| `setFilter(cycles)` | Ignores pulses shorter than about `cycles` F_CPU cycles (0 to 1024). For a noisy line. |
| `end()` | Gives the timer back. |

A `Pulse` holds:
- `period` and `high`, in ticks;
- `tickHz`.

It converts with:
- `periodMicros()`, `highMicros()` and `frequencyHz()`, as integers;
- `frequency()` and `duty()`, as floats. They are not linked unless used.

- **A period runs from one rising edge to the next.** If noise made an edge before the signal started, the first period is measured from that edge.
- **`read()` returns only the newest period.** Periods in between are dropped. To average, read several times.

## CH32RVEncoder

The timer runs in encoder mode. The names are ESP32Encoder's.

| Call | Meaning |
|---|---|
| `attachFullQuad(a, b)` | Counts every edge of A and B: 4 counts per cycle. |
| `attachHalfQuad(a, b)` | Counts both edges of A: 2 counts per cycle. |
| `attachSingleEdge(a, b)` | 1 count per cycle (the half-quad count halved). |
| `getCount()` | A 64-bit count that goes up when A leads B. |
| `setCount(v)` / `clearCount()` | Sets the count. |
| `pauseCount()` / `resumeCount()` | Stops and restarts counting. Edges while paused are not counted. |
| `setPull(mode)` | Call before `attach*()`. The default is `INPUT_PULLUP`, for encoders whose contacts go to GND. |
| `setFilter(cycles)` | Ignores pulses shorter than about `cycles`. For contact bounce. |
| `detach()` / `isAttached()` | Gives the timer back / reports whether it is held. |

- **Start.** `attach*()` starts counting at 0.
- **Beyond 16 bits.** Counts past the 16-bit counter are carried by the timer's update interrupt, inside the library.
- **Fast back-and-forth at the counter's end.** If the count crosses the end (0 / 65535) and comes back faster than that interrupt can run (twice within a few µs), the count can slip by 65536. Use `setFilter()` on bouncing contacts.

## CH32RVInputCapture

Each edge's time, latched by the capture in hardware: the time is right to the tick however late the interrupt runs. The interrupt extends the count to 64 bits and queues the edge (8 at most), or calls the sketch's callback.

| Call | Meaning |
|---|---|
| `begin(pin, edge = CHANGE, tickHz = 1000000, mode = INPUT)` | `edge` is `RISING`, `FALLING` or `CHANGE`. `tickHz` is the count's rate: the timer clock divided by a whole number (the nearest is taken), 1 MHz by default so ticks are microseconds. False when the pin is no timer channel's pad, every timer it reaches is taken, or `tickHz` is out of reach. |
| `read(edge)` / `available()` | The oldest queued edge: `ticks` (64 bits) and `rising`. |
| `onEdge(callback)` | Called from the timer's interrupt for each edge, in order, instead of queuing: a function or a small lambda taking `const CH32RVInputCapture::Edge &`. Keep it short. `nullptr` goes back to the queue. |
| `lost()` | Edges dropped: the queue was full, or two of a kind came closer than the interrupt could take them. |
| `now()` | The count now, on the edges' scale. |
| `setFilter(cycles)` | Call before `begin()`. Ignores pulses shorter than about `cycles` timer clocks. |
| `end()` / `isAttached()` | Gives the timer back / reports whether it is held. |

- **`CHANGE` uses two channels.** The pin's channel latches rising edges and its pair (1-2, 3-4) falling ones from the same input, so a rising and a falling edge never overwrite each other. The timer is held whole either way.
- **Edges closer than the interrupt.** Two edges of the same kind within a few microseconds: the newer is kept and `lost()` counts the other.

## timerPulseIn()

`timerPulseIn(pin, state, timeout = 1000000)` is `pulseIn()` by the timer's capture: the same arguments and result (the width in µs, 0 on the timeout), with both edges latched in hardware, so the width is exact to 1 µs whatever interrupts run during the pulse. The pin is a timer channel's pad on any route (its pair, 1-2 or 3-4, latches the other edge); the timer is borrowed whole for the call, politely. Where the pin is no such pad or the timers are taken, it is `pulseIn()`. `pulseIn()` itself stays the software loop for every pin and links no timer code: the capture costs about 2 KB of flash on V003 (the timer manager), so a sketch chooses it.

## What was checked

Checked on 2026-10-28 with no wiring. The same chip drove the measured pad itself, from GPIO, `tone()` and Servo. Series covered: V003, V006, V103, V203, V205, V307, L103, X035, X315 and H417. On every one, the timer read the pad's level while the pad was a GPIO output.

- **Period and high time:**
  - `tone()` from 37 Hz to 5 kHz, and Servo at 1500 µs;
  - a held line, a signal slower than `minHz`, and a channel 2 pad.
- **Encoder:**
  - x4, x2 and x1, and A and B swapped;
  - counts to ±70000, past 16 bits;
  - `setCount` and `pauseCount`.
- **Hand-back:** after the timer is given back, `analogWrite()` works on it.

`CH32RVInputCapture`, `timerPulseIn()`, the remapped pads and the channels 3 / 4 (2026-11-17) are built only so far; tests/instrumented/signals (`test_edge_capture`, `test_timer_pulse_in`) checks them against the probe's PWM.

## Examples

- [PulseMeasure](examples/PulseMeasure/README.md): measures frequency, period and pulse width.
- [RotaryEncoder](examples/RotaryEncoder/README.md): counts a rotary encoder.
- [EdgeTimes](examples/EdgeTimes/README.md): prints each edge's time and the high and low times between them.
