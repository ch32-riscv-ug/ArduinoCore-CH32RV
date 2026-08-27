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

```
probe-rs attach --chip CH32V003F4P6 <firmware.elf>
```

Any `pnum` from the board menu works as `--chip`. Pass the **ELF**: that is
where probe-rs looks up the control block.

**Not the IDE's serial monitor.** That one speaks to serial ports, and this is
not one; wiring RTT into it would need a pluggable monitor tool of our own
(docs/todo.ja.md). On Linux and macOS `socat` can bridge it into a pty if you
want the IDE window. Per-OS instructions are in
[docs/debug-output.ja.md](../../docs/debug-output.ja.md) (Japanese).

Measured on this bench with probe-rs 0.32.0: `download` and then `attach`
streams live while the target runs, on CH32V003 (18 lines in 23 s, and typed
input echoed back) and on CH32V203 alike. Attaching to a target the probe
happens to have left halted shows only what was printed before - reflash or
reset it and attach again.

## What it costs

RAM, which the other two debug channels do not use. Measured on CH32V003
against an empty sketch (624 bytes flash, 4 bytes RAM):

| | flash | RAM |
|---|---|---|
| not included | 0 | 0 |
| `SerialSDI`, for comparison | +364 | +20 |
| `SerialDMDATA`, for comparison | +700 | +36 |
| `SerialRTT`, buffers 256/16 (default) | +656 | +364 |
| `SerialRTT`, buffers 64/8 | +640 | +164 |

The buffers are the bulk of the RAM, and they are `#define`s:

```
-DCH32_RTT_UP_SIZE=64        // target to host, default 256
-DCH32_RTT_DOWN_SIZE=8       // host to target, default 16
```

Pass them through `build_opt.h` beside the sketch, or
`--build-property build.extra_flags=...` from arduino-cli. On a 2 KB part that
is worth doing; on a 20 KB one the default is nothing.

## Which debug channel to use

| | host tool | direction | cost |
|---|---|---|---|
| `SerialSDI` | wlink, WCH-LinkUtility | send only | none |
| `SerialDMDATA` | minichlink | two-way | none |
| **`SerialRTT`** | **probe-rs attach** | **two-way** | **RAM** |

`SerialSDI` and `SerialDMDATA` share the debug module's data registers and
cannot be used together. `SerialRTT` uses neither, so it can run alongside
either of them.

## Changing where printf() goes

```cpp
ch32_set_stdout(&SerialRTT);      // printf() into the ring buffer
ch32_set_stdout(&Serial);         // back to the UART
ch32_set_stdout(nullptr);         // discard
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
- **Not including it costs nothing.** Including it costs the instance, its
  vtable *and* the buffers even if the sketch never calls a method - that is
  the table above.

The control block layout is the publicly documented one and the symbol carries
the name the host tools look for. **No SEGGER code is used.**

## examples

- **HelloRTT** - the smallest output, with the host command at the top.
- **RttEcho** - reads what you type and sends it back, without blocking.
