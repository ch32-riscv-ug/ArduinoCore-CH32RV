# SerialRTT

Two-way serial through a ring buffer in RAM, **with no UART, no pin and no
wiring** - and the host side is the tool this core already flashes with.

The sketch keeps a small control block in RAM: a magic string, then a
descriptor per buffer holding its address, its size and the two offsets a ring
buffer needs. The probe finds it (by the `_SEGGER_RTT` symbol in the ELF, or by
scanning RAM) and then simply reads and writes that memory over the debug
transport. **The core is never halted**, and because the host can write a
second buffer, `read()` works.

```cpp
#include <SerialRTT.h>

void setup() {
  SerialRTT.begin(115200);          // the baud rate is ignored: no wire
  SerialRTT.println("hello");
}
```

## Reading it on the host

With the bundled ch32rv, through a WCH-Link or an OEP probe:

```
ch32rv monitor --source rtt --chip CH32V203
```

It finds the control block in RAM by itself (no ELF needed) and halts the
core briefly for each poll. In the IDE, pick the port and set the monitor's
`source` to `rtt`.
`probe-rs attach --chip <part> <firmware.elf>` works as well.

Per-OS instructions are in
[docs/debug-output.ja.md](../../docs/debug-output.ja.md) (Japanese).

If a probe has left the target halted, it produces no new output. Reflash or
reset it to resume execution before attaching.

## What it costs

Unlike the debug-register channels, RTT allocates buffers in RAM. Their sizes
are configurable `#define`s:

```
-DCH32RV_RTT_UP_SIZE=64        // target to host, default 256
-DCH32RV_RTT_DOWN_SIZE=8       // host to target, default 16
```

Pass them through `build_opt.h` beside the sketch, or
`--build-property build.extra_flags=...` from arduino-cli. Reduce them on
RAM-constrained targets.

## Which debug channel to use

| | host tool | direction | cost |
|---|---|---|---|
| `SerialSDI` | wlink, WCH-LinkUtility | send only | none |
| `SerialDMDATA` | ch32rv monitor --source dmdata, minichlink | two-way | none |
| **`SerialRTT`** | **ch32rv monitor --source rtt** | **two-way** | **RAM** |

`SerialSDI` and `SerialDMDATA` share the debug module's data registers and
cannot be used together. `SerialRTT` uses neither, so it can run alongside
either of them.

## Changing where printf() goes

```cpp
ch32rv_set_stdout(&SerialRTT);      // printf() into the ring buffer
ch32rv_set_stdout(&Serial);         // back to the UART
ch32rv_set_stdout(nullptr);         // discard
```

Only **stdio** follows. The name `Serial` is fixed at compile time, so
`Serial.println()` still goes wherever it went before.

## Worth knowing

- **Nothing hangs when no host is attached.** `write()` never waits: it fills
  what room there is and drops the rest, the way a UART with nobody listening
  does. Only `flush()` waits, and it gives up after a bounded spin.
- **Writing from an interrupt is not safe.** The write offset is published with
  a single store, but two writers can still interleave their bytes.
- The buffers survive `end()`: a host that is already attached keeps reading
  what is there rather than seeing the stream corrupt.
- Including it links the instance, its vtable and the buffers even if the
  sketch never calls a method.

The control block layout is the publicly documented one and the symbol carries
the name the host tools look for. **No SEGGER code is used.**

## examples

- **HelloRTT** - the smallest output, with the host command at the top.
- **RttEcho** - reads what you type and sends it back, without blocking.
