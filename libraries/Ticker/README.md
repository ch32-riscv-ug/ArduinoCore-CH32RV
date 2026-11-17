# Ticker

[日本語](README.ja.md)

Call a function every so often, or once after a while: arduino-esp32's Ticker API.

```cpp
#include <Ticker.h>

Ticker blink;

void setup() {
  pinMode(PA1, OUTPUT);
  blink.attach_ms(500, [] { digitalWrite(PA1, !digitalRead(PA1)); });
}

void loop() {}
```

| Call | Meaning |
|---|---|
| `attach(seconds, callback)`, `attach_ms(ms, ...)`, `attach_us(us, ...)` | Call it every period. |
| `once(seconds, callback)`, `once_ms(ms, ...)`, `once_us(us, ...)` | Call it once, after that long. |
| `... (period, void (*)(T), T arg)` | The same with one argument the size of a pointer or smaller. |
| `detach()` | Stop. |
| `active()` | It is attached (a `once` stops being active as it is called). |

The callback is a function or a lambda capturing at most two pointers ([CH32RVCallback.h](../../cores/arduino/CH32RVCallback.h)): no heap.

## How it runs

- **One timer for all.** Every Ticker of the sketch shares one hardware timer, `timerBegin(1000000)` of the core's timer API ([CH32RVHwTimer.h](../../cores/arduino/CH32RVHwTimer.h)): it is taken when the first Ticker starts and given back when the last one stops. Times are microseconds on its 64-bit count.
- **In the interrupt.** The callback runs in that timer's interrupt; on the ESP32 it runs in the esp_timer task. Keep it short, and leave `delay()`, `Serial` output and the like to `loop()`. A callback may attach, detach or restart its own Ticker or another.
- **No drift.** A periodic Ticker keeps its period from the first call: a call that comes late does not move the next one. A call that missed a whole period is skipped rather than made twice.
- **No timer free.** When `analogWrite()`, `tone()`, Servo or a library hold every timer, `attach*()` / `once*()` do nothing and `active()` is false.
- **Sleep.** In stop and standby the timer stops with the clock; the Tickers resume late.

## Status

Built for every series (2026-11-17); tests/instrumented/signals (`test_ticker`) checks the periods and a `once_us` against the probe's capture of a pin the callback toggles.

## Examples

- [Blinker](examples/Blinker/README.md): arduino-esp32's example - blink, switch the blinking on and off, slow it down once.
