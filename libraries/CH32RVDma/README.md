# CH32RVDma

[日本語](README.ja.md)

Memory-to-memory copies and fills by a DMA channel: waiting for the end, or running in the background while the CPU
keeps working, with a callback when it ends. Every series.

```cpp
#include <CH32RVDma.h>
CH32RVDma dma;
dma.begin();                          // a free channel of its own
dma.copy(dst, src, n);                // waits until it is copied
dma.onDone(done);                     // done() after each background job (in the channel's interrupt)
dma.copyAsync(dst, src, n);           // returns at once; busy() / wait()
dma.fill(dst, 0, n);                  // n bytes of one value
```

## API

| Function | What it does |
|---|---|
| `begin()` / `end()` / `isAttached()` | Takes / gives back a channel: one nothing holds (`ch32rv_dma_claim()`) whose interrupt has no linked handler, searched from the last channel down (the first ones carry the peripherals' fixed requests: Serial's DMA buffers, CH32RVAdcStream, SD_MMC). false when there is none |
| `dma()` / `channel()` | The DMA block and channel taken (0 before) |
| `copy(dst, src, bytes)` / `fill(dst, value, bytes)` | Waits for the end. false, with nothing moved, before `begin()`, while a job runs, or for 0 bytes |
| `copyAsync(dst, src, bytes)` / `fillAsync(dst, value, bytes)` | Starts and returns; the CPU is free meanwhile |
| `onDone(cb)` | `cb()` in the channel's interrupt when a background job ends: a function, or a lambda with a small capture (`[&buf]` and the like; CH32RVCallback); `nullptr` for none. Not called by the waiting forms (`copy()` / `fill()`) |
| `busy()` / `wait()` / `abort()` | Whether a job runs; wait for it (it is driven from there with interrupts masked too); stop it (what it moved stays) |

- Words (32 bit) when both addresses and the length are multiples of 4, halfwords for multiples of 2, else bytes.
- A long transfer goes in parts of 65535 units (CNTR's 16 bits), each next part started from the interrupt.
- Speed: on some parts the CPU's `memcpy()` is faster (a 4 KiB copy, 2026-11-19: X035 218 us by memcpy against 140
  us by DMA, V203 24 against 44, H417 28 against 53). A DMA copy pays when the CPU has other work meanwhile.
- A sketch that waits in its own WFI (`__asm__("wfi")`) stops the DMA meanwhile on V003, V203 and X035 (V006 and
  L103 go on, 2026-11-20). `wait()` uses no WFI.
- The interrupt is attached at run time with `ch32rv_irq_attach()` ([ch32rv_priority.h](../../cores/arduino/ch32rv_priority.h)).
  V103's vector table is jumps, where a linked handler cannot be told: used together with a library that has that
  channel's handler (CH32RVLogicCapture and the like), `onDone()` may not run on it (`busy()` / `wait()` work).

## Examples

| Example | What it does |
|---|---|
| [DmaCopy](examples/DmaCopy/README.md) | The CPU keeps counting during a copy, told of its end by the callback; a fill checked too |

Tests: selfapi's `test_dma` (a shifted byte copy, a word copy, a fill, a background copy with its callback, a fill
longer than 65535 units).
