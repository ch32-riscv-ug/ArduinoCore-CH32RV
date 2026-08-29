# SoftWire

日本語: [README.ja.md](README.ja.md)

Bit-banged I2C on any two pads, with the same API as [Wire](../Wire).

```cpp
#include <SoftWire.h>

SoftWire bus(PA1, PA2);          // SDA, SCL
```

**The bus still needs pull-up resistors** - 4.7k to 3V3 is the usual value.
I2C is open-drain, so nothing in software can substitute for them: without
pull-ups the lines never rise and every address times out.

## Why

The hardware I2C can only reach the pads its routes name. On the small CH32
parts those are often gone - not bonded out on your package, or already
carrying something else. A CH32V003 in SOP8 has six GPIO in total.

I2C is also the bus where bit-banging costs least. It is open-drain and slow by
design, and the controller owns the clock, so running under the nominal
100 kHz is not a protocol violation - it is just a slower transfer.

## Drop-in

`SoftWire` derives from `HardwareI2C`, the same base `Wire` uses, so anything
written against `TwoWire&` takes one unchanged:

```cpp
SoftWire bus(PA1, PA2);
Adafruit_Something device(&bus);
```

## What it does

- **True open-drain.** Both pads are `OUTPUT_OPENDRAIN`, which CH32's GPIO has
  natively, so HIGH is a genuine release rather than a switch to `INPUT`.
- **Clock stretching is honoured.** Releasing SCL is a request; the transfer
  waits until the line actually rises, so a target that needs a moment gets it.
  Getting this wrong is the classic bit-banged-I2C bug.
- **AVR-compatible return codes** from `endTransmission()`: 0 success, 1 data
  too long, 2 address NACK, 3 data NACK, 5 timeout.
- **The timeout API**, with the same two divergences `Wire` documents: it is on
  by default at 25 ms, and a timeout always ends in a stop condition.

## What it does not do

- **No clock frequency.** `setClock()` sets a *floor* on the half period, not a
  frequency anyone can hit. Asking for more than the loop already produces
  leaves it running flat out.
- **No target (slave) mode.** A target has to answer someone else's clock,
  which a busy loop cannot promise. `begin(address)` is accepted so that code
  from another core compiles, and the bus stays a controller.

## Cost

Measured on CH32V003 (16 KB flash), against the same sketch built with the
hardware `Wire`:

| | flash | RAM |
|---|---|---|
| hardware `Wire` | +2608 B | +152 B |
| `SoftWire` | +1904 B | +96 B |

`SoftWire` is the **smaller** of the two here - the peripheral's state machine,
error recovery and route programming cost more than bit-banging does. (SPI goes
the other way; see [SoftSPI](../SoftSPI).) Neither costs anything to a sketch
that does not include the header.
