# SerialSDI

Serial output through the debug module, with **no UART, no pin and no wiring**.

The two words `data0`/`data1` are the RISC-V debug module's abstract data
registers, mapped into the hart's address space - the address is what
`hartinfo.dataaddr` reports, so it is a fact the hardware states rather than a
magic number. A host (the bundled ch32rv, or the WCH-LinkE itself) reads them
over the debug wires. **The core is never halted.**

**The address differs per family**: `0xE00000F4` on the V2 families
(V003/V00x), `0xE0000340` on most V3 families (V205/V407/X315/M030) and
`0xE0000380` on the V4 families and V103. The board passes it as
`CH32RV_DM_DATA0_ADDR`, so **there is nothing for a sketch to configure**
(the source is `ch32-device-data`'s `evidence/debug_data.csv`).

```cpp
#include <SerialSDI.h>

void setup() {
  SerialSDI.begin(115200);          // the baud rate is ignored: no wire
  SerialSDI.println("hello");
}
```

## Reading it

**The Arduino IDE's serial monitor reads it as it is.** The monitor's default source, Auto, reads
SerialSDI's output together with `Serial` (the UART), from the bundled ch32rv 0.35.0 on. Pick the
`wchlink://...` port (a WCH-Link) or the `oep://...` port (an OEP probe). ch32rv reads the mailbox
directly over the debug wires, so **nothing on the probe is changed**.

```
ch32rv monitor --source sdi                    # SerialSDI only
ch32rv monitor --source auto                   # together with the UART and the other paths
```

Opening only the LinkE's COM port (the UART bridge) shows nothing from SerialSDI; pick the
`wchlink://...` port.

## Reading it through the LinkE's forwarding (WCH's own way)

The WCH-LinkE can also forward it itself: once forwarding is on, the probe sends what it collects to
**its own USB CDC port**. On a WCH-Link that is about twice as fast as ch32rv's direct read (V307: about
20 KB/s against 10 KB/s; an OEP probe reads it directly at about 28 KB/s). While it forwards, **the UART's output no longer reaches that CDC** (the UART
bridge stops).

- **A WCH-LinkE only.** Neither the original WCH-Link (CH549) nor the WCH-LinkW has it.
- **Only the series the probe forwards**: V003, V00x, V103, V20x, V30x, V317, X035, L103. Other series
  have the registers but the probe firmware will not poll them (ch32rv's direct read above works there).

Switch forwarding on yourself after uploading (every ch32rv command that opens the Link, the upload
included, turns forwarding off: the LinkE stops it on the probe-info query ch32rv sends first), then open the `wchlink://...` port (or the LinkE's COM
port) with the monitor's `uart` source (arduino-cli's `--config source=uart`, the Arduino CLI Wrapper for
VS Code, ...). Arduino IDE 2 cannot pick the monitor's source, and its default, Auto, turns the forwarding
off when it opens the Link, so the IDE cannot read through forwarding (Auto reads the same output
over the debug wires instead).

```
ch32rv monitor sdi on                          # switches forwarding on
ch32rv monitor sdi off                         # switches it off (the LinkE keeps it in RAM: power-off clears it too)
```

WCH's own tools switch it on too:

| | OS | |
|---|---|---|
| **WCH-LinkUtility** | **Windows only** | WCH's own. **Target** menu -> **Enable SDI Printf** |
| **OpenOCD bundled with MounRiver Studio** | Linux / macOS / Windows | Also WCH's. `openocd -f wch-riscv.cfg -c "sdi_printf enable" -c init -c exit` |
| **[wlink](https://github.com/ch32-rs/wlink)** | Linux / macOS / Windows | `wlink sdi-print enable` (`wlink flash --enable-sdi-print --watch-serial firmware.elf` flashes and watches in one go) |

With forwarding left on, the LinkE takes the characters first, so ch32rv's `sdi` and `auto` get nothing
(ch32rv warns `sdi-quiet` and points at `ch32rv monitor sdi off`).
**Details are in [docs/debug-output.ja.md](../../docs/debug-output.ja.md) (Japanese).**

## Retargeting printf()

```cpp
ch32rv_set_stdout(&SerialSDI);      // printf() now goes to the probe
ch32rv_set_stdout(&Serial);         // back to the UART
ch32rv_set_stdout(nullptr);         // discard
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

## Detailed example guides

- [HelloSDI](examples/HelloSDI/README.md): connections, operation, API use and adaptation notes.
- [PrintfToSDI](examples/PrintfToSDI/README.md): connections, operation, API use and adaptation notes.
