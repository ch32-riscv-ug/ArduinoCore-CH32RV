# SerialDMSeq

A two-way terminal over the debug module's data registers, **with no UART, no
pin and no wiring** - and one where **no byte is lost or doubled** on the way.

Same two registers as [SerialDMDATA](../SerialDMDATA/README.md) and
[SerialSDI](../SerialSDI/README.md) - the debug module's `data0`/`data1`,
mapped into the hart's address space - in the **dmseq** framing: a sequence
number each way, a SYN bit for a restarted target, and a CRC-8 on every word.
Up to **six bytes out** and **two bytes in** per frame. **The core is never
halted.**

```cpp
#include <SerialDMSeq.h>

void setup() {
  SerialDMSeq.begin(115200);        // the baud rate is ignored: no wire
  SerialDMSeq.println("hello");
}
```

## Why not SerialDMDATA

SerialDMDATA speaks minichlink's framing, which has no sequence numbers. When
the probe's answer to a frame silently fails to land - it happens over flying
wires - the probe reads the same frame again and the bytes arrive twice; and a
probe that rewrites its answer to cover for that cannot tell a re-read from the
sketch printing the same thing twice, so it drops characters instead. Both were
measured (a CH32L103 behind an RP2350, a CH32V003 behind an ESP32). dmseq tells
the two apart, and the CRC throws away anything corrupted, including the words
an attach or a flash leaves in `data0`.

The framing is specified in oep-spec `docs/target-console-dmseq.ja.md`
(`target.console` framing 2); the experiment that chose it, with fault
injection, is oep-spec `experiments/dm-console-seq`.

## Reading it on the host

```
ch32rv monitor --source dmseq
```

or an OEP probe's `target.console` with framing 2. **Not minichlink**: it speaks
SerialDMDATA's framing.

## It cannot share a sketch with SerialSDI or SerialDMDATA

All three write the same two registers, and a host reading one framing sees the
others as noise.

| | host tool | direction | integrity |
|---|---|---|---|
| `SerialSDI` | wlink, WCH-LinkUtility | send only | none |
| `SerialDMDATA` | minichlink, `ch32rv --source dmdata` | two-way | none |
| **`SerialDMSeq`** | **`ch32rv --source dmseq`, OEP** | **two-way** | **sequence + CRC** |
| `SerialRTT` | probe-rs attach | two-way | - |

## Receiving needs the sketch to poll

Input arrives only in the host's answers to frames the sketch posts. While the
sketch is printing, its data frames carry that; when it is idle, `available()`
posts an empty frame to invite the next two bytes. Call `available()` in
`loop()`. It does not post one right after a write, so a loop that alternates
`available()` and `print()` does not pay an extra round trip per print.

What arrives is parked in a 16-byte buffer (`CH32_DMSEQ_RX_SIZE`). With no room
for another frame the sketch does not take it, and the host sends it again -
nothing is lost.

## Worth knowing

- **Nothing hangs when no host is attached.** A write waits 20 ms
  (`CH32_DMSEQ_WAIT_MS`) until a host has answered once, 1 s
  (`CH32_DMSEQ_HOST_WAIT_MS`) after. On a timeout the frame stays posted with
  its TO bit set and later writes are dropped until a host answers it;
  `alive()` reports that and clears itself. A host that polls at least once a
  second loses nothing. The waits count register polls, not `millis()`, so they
  end with interrupts masked too.
- **It costs more than SerialDMDATA.** On CH32V003 against an empty sketch
  (624 bytes flash, 4 bytes RAM), a sketch that begins, reads and writes costs
  **+1268 bytes of flash and +52 bytes of RAM** (SerialDMDATA: +700 / +36). The
  CRC uses a 16-entry table: as fast as a 256-byte one, 24 bytes more than a
  bit loop.
- **Speed** (1270-byte stream, measured): 41 kB/s on a CH32X035 behind an
  ESP32-P4, 8.4 kB/s on a CH32V003 behind an ESP32 over SWIO, 9.2 kB/s on a
  CH32L103 behind an RP2350. Over a WCH-LinkE every access is a USB round trip,
  so expect a few kB/s. It is a console, not a data link.
- **The address differs per family**; the board states it from
  `ch32-device-data`, so there is nothing to configure.

## examples

- **HelloDMSeq** - prints once a second and echoes back what you type, uppercased.
