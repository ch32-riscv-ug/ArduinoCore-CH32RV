# CH32RVTaskKit

[日本語](README.ja.md)

Short pieces of work that run only when a trigger says so - a post from an interrupt, a deadline, a period, a condition - instead of being called on every `loop()`. **Optional**: a sketch that calls its steps from `loop()` in order does not need it.

```cpp
#include <CH32RVTaskKit.h>

CH32RVTaskKit::Executor app;
CH32RVSyncKit::Period every250(250);
void blinkStep() { /* do a little, return */ }
CH32RVTaskKit::Work blink(blinkStep);

void setup() { app.attach(blink); blink.wakeEvery(every250); }
void loop()  { app.runOnce(); }       // the one place work runs
```

## Principles

- **A Work is a function plus "should it run now?".** It has no stack, so it cannot wait in the middle: it does a bit, keeps its state in variables and returns. ESP32TaskKit and FreeRTOS tasks have stacks and can block; this does not, which is how a small CH32 does several things without an RTOS.
- **Work runs only inside `runOnce()`**, never from an interrupt or `delay()`.
- **One `runOnce()`** checks the triggers, then runs each ready Work once, in attach order. Work posted during the round runs in the next one.
- **No nesting**: `runOnce()` inside a Work does nothing and returns 0 (any executor).
- **No time promise**: a Work that does not return stops everything.
- **No heap, no capacity**: each Work is its own list node.

## Parts

| Part | Operations |
|---|---|
| `Work` | `post()` (from anywhere, an ISR too; several are one run), `wakeOn(deadline)`, `wakeEvery(period)`, `wakeWhen(condition)`, `measure(stats)` |
| `Executor` | `attach(work)` (ready at once: runs in the first `runOnce()`), `detach(work)`, `runOnce()`, `anyReady()`, `measure(roundStats)` |
| `WorkStats` | per Work `runs`, `totalUs`, `maxUs` (only when measured) |
| `RoundStats` | the executor's `rounds`, `emptyRounds`, `maxUs` |

Deadlines and periods are [CH32RVSyncKit](../CH32RVSyncKit/README.md)'s `Deadline` / `Period`.

## Times

The times include interrupts that ran meanwhile (not CPU time); maxima are what was seen, not bounds. `emptyRounds` counts rounds with nothing ready, not idle CPU time.

## Examples

[examples/README.md](examples/README.md). How this fits with interrupts, not waiting and callbacks: [the guide to putting a sketch together (Japanese)](../../docs/sketch-guide.ja.md).
