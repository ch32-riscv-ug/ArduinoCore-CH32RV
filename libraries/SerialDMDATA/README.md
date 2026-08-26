# SerialDMDATA

A two-way terminal over the debug module's data registers, **with no UART, no
pin and no wiring** - and 36 bytes of RAM.

Same two registers as [SerialSDI](../SerialSDI/README.md) - the debug module's
`data0`/`data1`, mapped into the hart's address space - but a different
agreement about what the bytes in them mean, and that agreement has a
host-to-target direction. The low byte of `data0` is a status word: bit 7 says
the target has left something there, bit 6 that it gave up waiting, and the low
bits carry the length. Up to **seven bytes out** and **three bytes in** per
handshake. **The core is never halted.**

```cpp
#include <SerialDMDATA.h>

void setup() {
  SerialDMDATA.begin(115200);       // the baud rate is ignored: no wire
  SerialDMDATA.println("hello");
}
```

## Reading it on the host

This is minichlink's framing, from [ch32fun](https://github.com/cnlohr/ch32fun).
**This core does not ship minichlink** - build it from that repository:

```
minichlink -T
```

The protocol is implemented here from its documented framing; no ch32fun code
is used.

## It cannot share a sketch with SerialSDI

Both write the same two registers, and a host reading one framing sees the
other as noise. Pick by the tool you have:

| | host tool | direction | cost |
|---|---|---|---|
| `SerialSDI` | wlink, WCH-LinkUtility | send only | none |
| **`SerialDMDATA`** | **minichlink** | **two-way** | **none** |
| `SerialRTT` | probe-rs attach | two-way | RAM |

`SerialRTT` uses neither register, so it can be used alongside this one.

## Receiving needs the sketch to poll

The host only writes the register **after it has taken something out of it**,
so a sketch that never leaves anything there never hears anything either.
`available()` handles that: it takes what arrived and leaves an empty frame
behind as the invitation for the next three bytes. Calling it in `loop()` is
what makes the channel two-way.

What arrives is parked in a 16-byte buffer (`CH32_DMDATA_RX_SIZE`), and the
host is held off once there is no room for another frame. The buffer is what
makes an echo loop work at all: the register holds one frame, and the sketch's
own next `print()` overwrites it, so without somewhere to put those bytes two
frames out of three are lost. A sketch that prints far more than it reads can
still overrun it - `available()` often enough is the cure.

## Changing where printf() goes

```cpp
ch32_set_stdout(&SerialDMDATA);   // printf() to the probe
ch32_set_stdout(&Serial);         // back to the UART
ch32_set_stdout(nullptr);         // discard
```

Only **stdio** follows. The name `Serial` is fixed at compile time, so
`Serial.println()` still goes wherever it went before.

## Worth knowing

- **Nothing hangs when no host is attached.** A write waits a bounded spin for
  the probe to take the previous frame, then gives up - and *latches* that in
  the status word, so every write after it is free instead of spinning again.
  `alive()` reports that state, and it clears itself when a host attaches.
- **The address differs per family** (`0xE00000F4` on V2, `0xE0000340` on most
  V3, `0xE0000380` on V4 and V103). The board states it, from
  `ch32-device-data`'s `evidence/debug_data.csv`, so there is nothing to
  configure.
- **It costs almost no RAM.** Measured on CH32V003 against an empty sketch
  (624 bytes flash, 4 bytes RAM), including it costs 700 bytes of flash and 36
  bytes of RAM - the instance, its vtable and the receive buffer. `SerialRTT`
  costs 364 bytes of RAM for the same job.
- Seven bytes per handshake is not fast. It is a tracing channel, not a
  replacement for a UART carrying real data.
- **Not including it costs nothing.** Including it costs the instance and its
  vtable even if the sketch never calls a method - that is the 700/36 above.

## examples

- **HelloDMDATA** - prints once a second and echoes back what you type.
