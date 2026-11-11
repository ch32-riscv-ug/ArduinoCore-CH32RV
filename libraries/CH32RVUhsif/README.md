# CH32RVUhsif

[日本語](README.ja.md)

The UHSIF (universal high-speed interface) of the CH32H415 / H416 / H417: a
clocked state machine on a 16-bit data bus that writes RAM through socket
DMA. WCH publishes no registers, only a binary library (`libUHSIF.a`); this
library does not use it and is built from measurements on the chip (the
notes, in Japanese: [docs/h417-uhsif.ja.md](../../docs/h417-uhsif.ja.md)).

To capture like a logic analyzer, [CH32RVLogicCapture](../CH32RVLogicCapture/README.md)'s
`begin(UHSIF_DATA, rate)`, built on this library, is the simple way.

## What it does (checked on CH32H417, 2026-11-11)

- Captures the data lines, clocked internally from SYSCLK (400 MHz) or the
  USBHS PLL (480 MHz, from the crystal), divided by 1 to 64.
- A state machine: up to 255 states with two ways out each, and up to 16
  different truth tables for the right way out.
- A data counter.
- Four threads, each writing a ring of buffers without the CPU.
- An interrupt raised by the state machine.

Not yet: driving the bus, an external clock, the control lines as outputs,
8- and 32-bit widths.

## Pins

| Data line | Pin |
|---|---|
| DATA0-5 | PD10-PD15 |
| DATA6-8 | PF0-PF2 |
| DATA9-12 | PC6-PC9 |
| DATA13-15 | PA13-PA15 |

They are read whatever their pin mode (input, an alternate function or GPIO
output); `begin()` leaves them alone. Inputs go up to the pin's own supply.

## Use

```cpp
#include <CH32RVUhsif.h>

CH32RVUhsif uhsif;
static uint16_t ring[8192] __attribute__((aligned(32)));

uhsif.begin(63);                                  // SYSCLK / 64 (USBHS PLL: begin(2, CH32RVUhsifClock::UsbhsPll))
uhsif.setStates(states, 2, start);                // the state machine
uhsif.setDataCounter(2048 - 2);                   // limit: one buffer
uhsif.setRing(0, ring, 4096, 4, 4096);            // thread 0: 4 x 4096 bytes
uhsif.onInterrupt([] { /* the machine's Interrupt */ });
uhsif.start();
```

The whole sketch is the [UhsifStates](examples/UhsifStates/README.md) example.

### States and transitions

- Each clock, a state leaves by its right transition when `goRight` is true,
  else by its left one.
- `goRight` is a truth table over the state's four inputs a-d (bit
  (a + 2b + 4c + 8d)); 0 means always left.
- A transition names the state it enters and the outputs that go with it
  (alpha and beta).
- `start()` enters the first state through the `start` transition.

| Output | Meaning |
|---|---|
| `SampleData` (alpha) | latch the data lines |
| `Push` | hand the latched value to the thread's socket |
| `CountData` / `ResetDataCount` | data counter + 1 / to 0 |
| `Interrupt` | call the `onInterrupt()` callback |
| `thread(n)` | which thread takes the push (0-3) |

| Input | Meaning |
|---|---|
| `Ctrl0` + n | control line CTRLn (not measured) |
| `DataCountHit` | the data counter is at its limit |
| `DmaReady` | the current thread's socket can take data |
| `One` | always 1 |

Rules of the hardware (measured):

- A transition into the state it leaves does not give alpha again: to
  sample every clock, alternate between two states.
- A `ResetDataCount` is not yet seen by the decision of the state it
  enters: let a state entered by a reset leave by `left` without looking at
  the counter.
- Set the data counter's limit to a buffer's samples minus 2 (8190 for
  8192).

### Rings of buffers

- `setRing(n, first, bytes, count, stride)` makes thread n write `count`
  buffers (buffer k at `first + k * stride`) in turn, round and round.
- `bytes` is a multiple of 64 up to 65472. Keep the buffers in RAM the DMA
  reaches (global or static arrays), 32-byte aligned. Sizes that were not a
  multiple of 32 stopped the CPU (the DMA wrote outside the buffer, we
  think).
- A socket stops after each buffer, and the library's interrupt sends it on
  to the next descriptor. Samples pushed to it meanwhile are dropped, so a
  thread must not come back to it before that.
- Two threads in turn lose none: one writes while the other waits to be
  sent on. That is what CH32RVLogicCapture's UHSIF capture does. Make a
  buffer last 20 us or more.
- A socket left running through its ring (no transfer size) works too, but
  it moved on 16 bytes before the end of every buffer and those 8 samples
  were lost; the library does not use that.

### Stopping

`stop()` stops by resetting the UHSIF. That also clears the setup
(`begin()`, `setStates()`, `setDataCounter()`, `setRing()`): set it up again
before the next `start()`. The buffers keep what was written.

## Errors

Operations return `bool`; `lastError()` and `lastErrorName()` say why.

| `CH32RVUhsifError` | Meaning |
|---|---|
| `NotBegun` | before `begin()` |
| `ClockFailed` | the USBHS PLL did not lock (no crystal?) |
| `InvalidArgument` | a bad index, size (multiple of 64) or alignment (32 bytes) |
| `TooManyFunctions` | more than 16 different `goRight` tables |
| `Running` | the setup cannot change while running (`stop()` first) |

## Other notes

- There is one UHSIF: only one object can be begun at a time.
- Into SRAM, two threads ran at 400 MHz (800 MB/s),
  but at 400 MHz one period of a 390 kHz PWM came split in two (taken near
  an edge, we think); at 200 MHz and below none did.
- The RM rates the UHSIF to 125 MHz. Fast signals (setup and hold, skew
  across the 16 lines) have not yet been checked with an outside source.
- With an interrupt per buffer, keep them 20 us or more apart: at 200 MHz,
  one every 256 samples left the CPU doing nothing but the handler.

## Examples

- [UhsifStates](examples/UhsifStates/README.md): program the state machine,
  sample the data lines every clock and count the buffers.
