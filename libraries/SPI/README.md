# SPI

Controller-mode SPI on the CH32's own peripheral, polled. Name the pins with
`SPI.begin(sck, miso, mosi, ss)`; chip select is an ordinary GPIO, as on every
Arduino core.

```cpp
#include <SPI.h>

void setup() {
  pinMode(SS, OUTPUT);
  digitalWrite(SS, HIGH);
  SPI.begin(SCK, MISO, MOSI, SS);   // the pads you wired

  SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
  digitalWrite(SS, LOW);
  uint8_t reply = SPI.transfer(0x9F);
  digitalWrite(SS, HIGH);
  SPI.endTransaction();
}
```

## Which pins

The bus takes whichever free SPI has the pins as a route; a name is not tied to
an SPI number, and `peripheral()` tells which SPI a bus landed on.

`SPI.begin()` without pins is the fallback: the board's enabled SPIs in number
order (`SPI` the first, `SPI1` the second, `SPI2` the third) on their default
pins - the board menu's FIX pins, or the variant's default route under AUTO.

`SCK`, `MISO` and `MOSI` name the first SPI's default route. `SS` names the
peripheral's own NSS pad where device-data has one - the driver never uses it,
because chip select is a GPIO, but it is the pad the wiring diagrams show.

To move the bus:

```cpp
SPI.setRoute(1);
SPI.setPins(PB3, PB4, PB5);       // SCK, MISO, MOSI - all from one route
```

Both return `false` and change nothing if the route does not exist, and
`setPins()` refuses pins that do not all belong to the same route.

## Things worth knowing

- **The clock is rounded down, never up.** `SPISettings(1000000, ...)` on a
  48 MHz part gives 750 kHz, because the divider is a power of two and going
  over a device's maximum is what breaks it.
- **MISO is pulled up by `begin()`.** With nothing connected a transfer reads
  back `0xFF` instead of noise, which makes "no device" look like no device.
- **`transfer16()` sends two 8-bit frames**, honouring the bit order, rather
  than switching the peripheral to 16-bit. Mixing it with `transfer()` cannot
  leave the frame size half-changed.
- **`usingInterrupt()` does nothing.** This driver never touches the bus from
  an interrupt, so there is no shared state to protect. A sketch that uses SPI
  *from* its own ISR still has to arrange that itself.
- **Peripheral (slave) mode is not implemented**, and
  `SPI_HAS_PERIPHERAL_MODE` is deliberately left undefined so a library can
  test for it.
- **Names are `SPI`, `SPI1`, `SPI2`**, one per SPI the board enables. Using a
  name the board does not have fails to compile.
- The legacy `setBitOrder()` / `setDataMode()` / `setClockDivider()` are
  provided, because enough libraries still call them.

## Examples

- **SPILoopback** - one jumper from MOSI to MISO proves the clock, the data
  line and the framing in one go.

## Detailed example guides

- [SPILoopback](examples/SPILoopback/README.md): connections, operation, API use and adaptation notes.
- [SpiFlashId](examples/SpiFlashId/README.md): Read an external SPI Flash JEDEC ID without erase or programming.
