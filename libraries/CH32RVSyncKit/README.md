# CH32RVSyncKit

[日本語](README.ja.md)

Small parts that connect interrupts and `loop()`: a queue, notifications (a counter and bits), a binary semaphore, byte and message buffers, a block pool, a short lock, critical sections, deadlines and periods. For "the interrupt only signals, the work happens in normal code" on one core, without an RTOS.

```cpp
#include <CH32RVSyncKit.h>

CH32RVSyncKit::Queue<uint32_t, 8> presses;      // 8 values, shared with an ISR

void onPress() { presses.trySend(millis()); }   // ISR: hand it over, return

void loop() {
  uint32_t t;
  while (presses.tryReceive(t)) {               // take what arrived, go on
    Serial.println(t);
  }
}
```

## Principles

- **Nothing waits.** Every operation tries once and returns. Full, empty or taken means "not now"; the caller keeps its state and tries again later. There is no `receive(timeout)`: waiting on one core stops everything else it does.
- **Sharing with an ISR is declared.** The second (or third) template argument chooses it.
  - `IsrSafe` (default): this core's interrupts are masked for the few instructions of each operation, so an ISR and `loop()` may both use the object. CH32V003 has no atomic instructions; this is the only way there.
  - `LoopOnly`: no masking. For objects used from `loop()` and what it calls only. Smaller and faster; touching one from an ISR is a bug.
- **Unused parts cost nothing.** Header-only templates: an object you do not declare is not linked. No heap.
- **One core only.** CH32H41x's other core keeps running while this one masks its interrupts; these parts are not for passing data between the cores.

## Parts

| Part | Main operations | For |
|---|---|---|
| `Queue<T, N>` | `trySend`, `trySendToFront`, `tryReceive`, `tryPeek`, `count`, `clear`, `overwrite` (`N == 1` only) | values in order; full refuses the new one |
| `Notify<>` | `notify`, `tryTake`, `tryTakeAll` / `setBits`, `tryWaitBits(mask, clearOnExit, waitAll)` | a count, or which kinds (bits) happened |
| `BinarySemaphore<>` | `give`, `tryTake` | "it happened" once; a second `give` before the take is false |
| `Ring<N>` | `write` (what fits), `read`, `available`, `space` | a byte stream |
| `MessageBuffer<N>` | `trySend` (all or nothing), `tryReceive`, `nextLength`, `count` | messages of varying length; each costs its length + 2 |
| `BlockPool<T, N>` | `tryAllocate`, `release` (false for a double or foreign release), `available` | lending big records; pair with `Queue<T *, N>` |
| `TryLock<>` | `tryLock`, `unlock`, `Guard` | holding a resource for a moment; nobody waits |
| `CriticalSection` | masks interrupts for a scope (nests) | reading several ISR-written variables as one |
| `Deadline` | `arm(ms)`, `expired`, `remaining`, `disarm` | timeouts, retries |
| `Period` | `due`, `missed`, `start` | repeating work; late periods are skipped and counted, not run in a burst |

`Deadline` and `Period` read `millis()` (or take a `now`). Both are only as punctual as `loop()`.

## Critical sections

A `CriticalSection`, and each `IsrSafe` operation, holds off every interrupt of this core (USB, USB PD, the UARTs, SysTick) while it lasts.

- Copy inside; compute and print outside.
- Never `delay()`, `Serial.print()` or wait in a loop inside one.
- Inside an ISR interrupts are already off, so the ISR needs no `CriticalSection` (one does no harm).

## ESP32SyncKit

Names follow [ESP32SyncKit](https://github.com/tanakamasayuki/ESP32SyncKit), so the same code style carries on to ESP32.

| ESP32SyncKit | CH32RVSyncKit |
|---|---|
| `Queue<T> q(depth)`, `send(v, timeout)` / `trySend(v)` | `Queue<T, N> q`, `trySend(v)` only; the depth is a template argument |
| `Notify`: `notify` / `take` / `takeAll` / `setBits` / `waitBits` | `Notify<>`: `notify` / `tryTake` / `tryTakeAll` / `setBits` / `tryWaitBits` |
| `BinarySemaphore`: `give` / `take(timeout)` | `BinarySemaphore<>`: `give` / `tryTake` |
| `Mutex` + `LockGuard` | none, as there are no tasks; `TryLock<>` + `Guard`, `CriticalSection` |
| detects an ISR by itself | `IsrSafe` / `LoopOnly`, declared |

## Examples

See [examples/README.md](examples/README.md). How the pieces fit into a whole sketch: [the guide to putting a sketch together (Japanese)](../../docs/sketch-guide.ja.md).

## License

The same as the core's.
