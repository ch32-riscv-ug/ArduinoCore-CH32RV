# SerialDMDATA

A two-way terminal over the debug module's data registers, **with no UART, no
pin and no wiring**.

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

```
ch32rv monitor --source dmdata
```

In the IDE, select the probe port and set the monitor `source` to `dmdata`.
[ch32fun](https://github.com/cnlohr/ch32fun)'s minichlink can also read this
framing. It is not bundled; build it from that repository and run
`minichlink -T` if you use it.

The protocol is implemented here from its documented framing; no ch32fun code
is used. Per-OS instructions are in
[docs/debug-output.ja.md](../../docs/debug-output.ja.md) (Japanese).

## It cannot share a sketch with SerialSDI

Both write the same two registers, and a host reading one framing sees the
other as noise. Pick by the tool you have:

| | host tool | direction | cost |
|---|---|---|---|
| `SerialSDI` | wlink, WCH-LinkUtility | send only | none |
| **`SerialDMDATA`** | **ch32rv monitor --source dmdata, minichlink** | **two-way** | **none** |
| `SerialRTT` | ch32rv monitor --source rtt | two-way | RAM |

`SerialRTT` uses neither register, so it can be used alongside this one.

## Receiving needs the sketch to poll

The host only writes the register **after it has taken something out of it**,
so a sketch that never leaves anything there never hears anything either.
`available()` handles that: it takes what arrived and leaves an empty frame
behind as the invitation for the next three bytes. Calling it in `loop()` is
what makes the channel two-way.

What arrives is parked in a 16-byte buffer (`CH32RV_DMDATA_RX_SIZE`), and the
host is held off once there is no room for another frame. The buffer is what
makes an echo loop work at all: the register holds one frame, and the sketch's
own next `print()` overwrites it, so a receive buffer is required to retain
input. A sketch that prints far more than it reads can
still overrun it - `available()` often enough is the cure.

## Changing where printf() goes

```cpp
ch32rv_set_stdout(&SerialDMDATA);   // printf() to the probe
ch32rv_set_stdout(&Serial);         // back to the UART
ch32rv_set_stdout(nullptr);         // discard
```

Only **stdio** follows. The name `Serial` is fixed at compile time, so
`Serial.println()` still goes wherever it went before.

## Worth knowing

- **Nothing hangs when no host is attached.** A write waits a bounded time for
  the probe to take the previous frame, then gives up - and *latches* that in
  the status word, so every write after it is free instead of spinning again.
  `alive()` reports that state, and it clears itself when a host attaches.
- **A slow host does not lose lines.** The wait is short (`CH32RV_DMDATA_WAIT_MS`,
  20 ms) until a host has taken something, and long (`CH32RV_DMDATA_HOST_WAIT_MS`,
  1 s) after, so a probe that polls late - several probes over USB/IP at once -
  is waited for instead of having frames dropped. Both are counted in register
  polls, not by `millis()`, so they still end with interrupts masked.
- **The address differs per family** (`0xE00000F4` on V2, `0xE0000340` on most
  V3, `0xE0000380` on V4 and V103). The board states it, from
  `ch32-device-data`'s `evidence/debug_data.csv`, so there is nothing to
  configure.
- RAM is used by the instance, its vtable and the receive buffer. Configure the
  receive-buffer size with `CH32RV_DMDATA_RX_SIZE`.
- Seven bytes per handshake is not fast. It is a tracing channel, not a
  replacement for a UART carrying real data.
- Including it links the instance and its vtable even if the sketch never calls
  a method.

## examples

- **HelloDMDATA** - prints once a second and echoes back what you type.
