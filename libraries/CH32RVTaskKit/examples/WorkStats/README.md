# WorkStats

日本語: [README.ja.md](README.ja.md)

Which piece of work is slow? Each Work counts and times its runs and the executor counts its rounds. One Work takes about 3 ms on purpose.

## Hardware and connections

A 115200-baud 8N1 UART monitor on RX/TX.

## Steps and expected results

1. Every 2 s: `FAST runs=... avg_us=... max_us=...`, `SLOW ...`, `ROUNDS total=... empty=... max_us=...`.
2. `SLOW`'s `max_us` is about 3000 and the rounds' `max_us` at least that: the slow Work holds the others up.

## Settings and limits

Times include interrupts that ran meanwhile (not CPU time); the maximum is what was seen, not a bound. `empty` counts rounds with nothing ready, not idle CPU time. Work and executors not measured pay nothing.

## API usage and adaptation

`work.measure(stats)` and `app.measure(rounds)` attach statistics storage. Check a nonzero run count before computing an average; `clear()` starts a new interval. Keep statistics objects alive during measurement.

One function is deliberately slow here. Split long work in applications and reduce logging overhead too. An observed maximum does not guarantee a worst-case real-time bound.

## Related documentation

- [Shared setup](../README.md)
- [CH32RVTaskKit API](../../README.md)

[WorkStats.ino](WorkStats.ino)
