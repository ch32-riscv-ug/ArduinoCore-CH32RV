# AdcTriggered

Japanese: [README.ja.md](README.ja.md)

Converts one analog pin once per tick of a timer at 1000 conversions a second, prints each second how many came in, then switches to one conversion per rising edge of the first pin that can trigger the ADC on this chip, and prints every conversion as it lands.

## What you need and the wiring

The voltage to measure on `INPUT_PIN` (default `A0`, 0 V to the supply). For the edge part, a switch or a signal on the trigger pin the sketch prints (`ADC_EDGE trigger pin=...`; which pins can be one depends on the chip - the [library README](../../README.md)); the sketch pulls it down. A UART monitor at 115200 baud 8N1 on `RX` / `TX`.

## What happens

1. At start: `ADC_TIMER rate=1000 timer=TIM3 step=7` (the timer the core took and the sample-time step; the timer differs between families).
2. Each second: `ADC_TIMER conversions=... (+1000) last=...` - a thousand conversions a second exactly, on the timer's clock.
3. After five seconds, when the chip has a trigger pin: `ADC_EDGE trigger pin=...`, then `ADC_EDGE <n> value=...` for each rising edge on that pin, the value of `INPUT_PIN` at the edge.

## Settings and limits

- `TIMER_RATE` can be anything a timer reaches, also far below the slowest free-running rate (`begin()`), down to fractions of a hertz.
- A timer is shared with `analogWrite()`, `tone()`, `timerBegin()` and the rest through the core's timer manager: `beginTimer()` fails when none of the family's trigger timers is free.
- Values are the converter's own, without `analogRead()`'s scaling.

## Related

- [CH32RVAdcStream API](../../README.md)

[AdcTriggered.ino](AdcTriggered.ino)
