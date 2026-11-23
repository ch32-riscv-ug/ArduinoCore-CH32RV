# CH32RVQspi

[日本語](README.ja.md)

The quad-SPI controller (QSPI1 of CH32V205, QSPI1 / QSPI2 of CH32H41x): commands to an external serial flash or any QSPI device, phase by phase - instruction, address, alternate bytes, dummy clocks, data - each on one, two or four lines. No Arduino or arduino-esp32 counterpart.

```cpp
#include <CH32RVQspi.h>

CH32RVQspi qspi;                                   // QSPI1 (QSPI2: qspi2(2))

void setup() {
  qspi.begin(PA12, PB0, PA2, PA3, PA0, PA1);       // SCK, CS, IO0-IO3 (IO2 / IO3 for 4-line phases only)
  qspi.setClock(10000000);

  CH32RVQspiCommand id;                            // 9F: the JEDEC ID, three bytes on one line
  id.instruction = 0x9F;
  uint8_t jedec[3];
  qspi.read(id, jedec, 3);
}
```

| Function | Meaning |
|---|---|
| `begin(sck, cs, io0, io1, io2 = -1, io3 = -1)` | The pins. CS on the SCSN pad: the first flash (IO on SIO0-3); on SCSXN: the second (SIOX0-3) |
| `setClock(hz)` | The fastest HCLK / 1 .. 256 at or under hz; returns the rate |
| `setMode(0 / 3)` | The SPI mode (this controller has 0 and 3) |
| `command(cmd)` | A command with no data phase (write enable, an erase with its address) |
| `write(cmd, data, n)` / `read(cmd, data, n)` | The command, then n bytes out / in on `cmd.dataLines` |
| `memoryMap(readCmd, flashBytes)` / `mapped()` | The flash read as memory (CH32H41x's QSPI1 at 0x90000000 only; elsewhere the address is not in the documents: false). Any other call ends it |
| `end()` | Stops, the pins back to inputs |

`CH32RVQspiCommand`'s fields: `instruction`, `instructionLines` (0, 1, 2, 4), `address`, `addressLines` (0: none), `addressBytes` (1 .. 4, 3 by default), `alternate` / `alternateLines` / `alternateBytes`, `dummyCycles` (0 .. 31), `dataLines` (1, 2, 4).

- Each pad's function is its own (V205, H41x); the variant's `CH32RV_QSPIn_PADS` lists them. CH32H416 bonds QSPI2's pads only; CH32H415 has no QSPI pad.
- 4-line phases need IO2 / IO3 (CR's SIOXEN is set for them).
- Polled; each phase gives up after 100 ms.

## Examples

- [QspiFlashInfo](examples/QspiFlashInfo/README.md): a flash's JEDEC ID and reads on one, two and four lines.

## Tests

periph::test_qspi: on the first flash's SCK, CS, IO0 and IO1, an erase (20), a page write (02), a dual write (A2), a fast read (0B, 8 dummy) and a dual read (BB, 4 dummy), each judged phase by phase by WireSkein's qspi (no device; the probe's pull-ups make the reads ff). Passes on V205 (2026-11-23). Quad, a real flash, the memory mapping and the second flash are unchecked (they need lines and parts); H41x built only.
