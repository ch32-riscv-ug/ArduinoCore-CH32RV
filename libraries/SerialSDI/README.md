# SerialSDI

Serial output through the debug module, with **no UART, no pin and no wiring**.

The two words `data0`/`data1` are the RISC-V debug module's abstract data
registers, mapped into the hart's address space - the address is what
`hartinfo.dataaddr` reports, so it is a fact the hardware states rather than a
magic number. A WCH-LinkE that has been told to do so polls them and forwards
what it finds to its own USB serial port. **The core is never halted.**

**The address differs per family**: `0xE00000F4` on the V2 families
(V003/V00x), `0xE0000340` on most V3 families (V205/V407/X315/M030) and
`0xE0000380` on the V4 families and V103. The board passes it as
`CH32_DM_DATA0_ADDR`, so **there is nothing for a sketch to configure**
(the source is `ch32-device-data`'s `evidence/debug_data.csv`).

```cpp
#include <SerialSDI.h>

void setup() {
  SerialSDI.begin(115200);          // the baud rate is ignored: no wire
  SerialSDI.println("hello");
}
```

## Nothing comes out until you switch it on (that is correct)

This library **only works on a WCH-LinkE, and only after the probe has been
told to forward**. With nothing configured you get not one character - that is
not a fault, it is the probe not forwarding yet. Two conditions:

- **A WCH-LinkE.** Neither the original WCH-Link (CH549) nor the WCH-LinkW has it
- **A chip the probe supports**: V003, V00x, V103, V20x, V30x, V317, X035, L103

Once it is on, the probe forwards what it collects to **its own USB CDC port**,
so the ordinary serial monitor is where to read it - no special monitor needed.

**That port is the UART bridge as well.** If the sketch also uses
`Serial.println()`, both streams arrive **mixed together** in one monitor
window. They cannot be separated; use one or the other if that matters.

## Switching it on

probe-rs cannot do it, so this one step needs another tool.

| | OS | |
|---|---|---|
| **WCH-LinkUtility** | **Windows only** | WCH's own. **Target** menu -> **Enable SDI Printf** |
| **OpenOCD bundled with MounRiver Studio** | Linux / macOS / Windows | Also WCH's. `openocd -f wch-riscv.cfg -c "sdi_printf enable" -c init -c exit` |
| **[wlink](https://github.com/ch32-rs/wlink)** | Linux / macOS / Windows | `wlink sdi-print enable` |

```
wlink sdi-print enable          # once; forwarding outlives the wlink process
arduino-cli monitor -p /dev/ttyACM4 -b ch32-riscv-ug:ch32v:CH32V003
```

wlink can also do the flashing and the watching in one go:

```
wlink flash --enable-sdi-print --watch-serial firmware.elf
```

The port is the WCH-LinkE's own CDC (`1a86:8010`); in the IDE, pick that same
port. **Per-OS instructions are in
[docs/debug-output.ja.md](../../docs/debug-output.ja.md) (Japanese).**

Other series have the registers but the probe firmware will not poll them.

## Retargeting printf()

```cpp
ch32_set_stdout(&SerialSDI);      // printf() now goes to the probe
ch32_set_stdout(&Serial);         // back to the UART
ch32_set_stdout(nullptr);         // discard
```

This moves **stdio only**. The name `Serial` is bound at compile time and does
not follow, so `Serial.println()` still goes where it always did.

## Things worth knowing

- **Nothing hangs when no host is listening.** The vendor's implementation
  waits forever for the probe to take a frame; this one gives up after a
  bounded spin and drops the bytes, the way a UART with nobody attached does.
  Unplugging the debugger cannot wedge a sketch.
- **Transmit only.** The protocol has a host-to-target direction, but nothing
  here has been verified for it, so `read()` always returns -1.
- Seven bytes go per frame, and the probe has to take each one before the next
  is written - it is not fast, and it is not meant to replace a UART for bulk
  output.
- The instance costs nothing unless a sketch mentions it.

## Examples

- **HelloSDI** - the smallest thing that prints, with the host command in the
  header.
- **PrintfToSDI** - move `printf()` to the probe and back.
