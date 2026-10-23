# ButtonBlinkConsole

日本語: [README.ja.md](README.ja.md)

A button, a blinking LED and UART commands in one sketch: everything the
[guide to putting a sketch together (Japanese)](../../../../docs/sketch-guide.ja.md) builds up to.
Four pieces of work, each started by a different trigger:

| Work | Trigger | What it does |
|---|---|---|
| `blink` | a `Period` | toggles the LED |
| `button` | `post()` from the ISR | handles the press times the ISR put in a `Queue` |
| `console` | a condition (the UART has bytes) | reads a line at a time and runs commands |
| `resume` | a `Deadline` | ends a pause |

No Work waits, and `loop()` is one `runOnce()`. The ISR only records and returns; the one place
interrupts are turned off is where the two counters it writes are read.

## Hardware and connections

- An LED with its resistor on `LED_PIN` (default `PC0`).
- A push button between `BUTTON_PIN` (default `PA1`) and GND; the internal pull-up is used.
- A 115200-baud 8N1 UART monitor on `RX` / `TX`, sending a line feed at the end of a line.

Change the pins in the `.ino`. Runs on every series, CH32V003 included.

## Steps and expected results

1. At start: `ButtonBlinkConsole ready: rate <ms>, pause, stats`, and the LED toggles every 0.5 s.
2. Press the button: `PRESS ms=... paused` and the LED stops; press again: `... running`.
   Interrupts from contact bounce within 50 ms count as one press.
3. Send `rate 100`: `RATE ms=100`, and the LED blinks faster (20 to 5000 ms).
4. Send `pause`: `PAUSE 5 s`, the LED stops, and after 5 s `RESUME`.
5. Send `stats`: the ISR's count and last time (`ISR count=... last_ms=...`), each Work's runs and
   longest run (`BLINK runs=... max_us=...` and so on), and the rounds (`ROUNDS total=... empty=...`).
   `ISR count` above the number of presses is contact bounce.

## Settings and limits

- The press times are data, in the `Queue`; "please handle them" is `post()`. Posts are not counted,
  so one run of `button` drains every queued time.
- A press when the `Queue` is full (8 entries) is dropped: the ISR does not wait.
- `isrCount` and `isrLastMs` are written together by the ISR, so `stats` copies both inside a
  `CriticalSection` and prints outside it.
- A Work that takes long delays the others; `max_us` in `stats` shows which one. The times include
  any interrupt that ran meanwhile.

## API usage and adaptation

`wakeEvery()`, `wakeWhen()`, `wakeOn()` and `post()` define when work is eligible; `runOnce()` executes it. Period/deadline objects do not create separate tasks.

When adding commands, preserve incremental line accumulation and avoid waiting for an operation to finish inside a command. Split long work into start/progress/completion states. Copy ISR-shared multi-field values together with `CriticalSection`, then print outside.

## Related documentation

- [How a sketch is put together (Japanese)](../../../../docs/sketch-guide.ja.md)
- [Shared setup](../README.md)
- [CH32RVTaskKit API](../../README.md)
- [CH32RVSyncKit API](../../../CH32RVSyncKit/README.md)

[ButtonBlinkConsole.ino](ButtonBlinkConsole.ino)
