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

## SoftSPI (SPI on any three pads)

The hardware SPI reaches only the pads its routes name. `SoftSPI` is a
bit-banged controller on any three pads. It is an `SPIClass`, like `SPI`, so a
library that takes `SPIClass *` takes it unchanged. It comes with the same
`<SPI.h>` (or `<SoftSPI.h>`).

```cpp
#include <SPI.h>

SoftSPI bus(PA1, PA4, PA2);       // SCK, MISO, MOSI (SPI.begin()'s order)
Adafruit_Something device(&bus);
```

- **The class says software or hardware.** `SPI.begin(sck, miso, mosi)` takes
  a hardware SPI only, and returns `false` for pins no route has.
- MISO can be `NOT_A_PIN` (-1 to `begin()`) for a write-only bus; `transfer()`
  then returns 0.
- **No clock frequency.** SPISettings' frequency is accepted and ignored; the
  one knob is `setHalfPeriodUs()`, a **floor** on the half period.
- All four modes, `MSBFIRST` and `LSBFIRST`. `transfer16()` / `transfer32()`
  send the low byte first under `LSBFIRST`, as `SPI` does.
- No CS and no target mode (CS is an ordinary GPIO, as on `SPI`). There is no
  `setRoute()`; `peripheral()` returns 0.

### Making SPI software from the board menu

Choose **Software** under **SPI1** (SPI2, SPI3) in the board menu and the name
that SPI backs (`SPI`, `SPI1`, `SPI2`) is a `SoftSPI`. `begin()` uses that SPI's
default route pins and `begin(sck, miso, mosi)` takes any pads; no hardware SPI
code is linked. It lets a library that uses the global `SPI` run on a board
whose hardware SPI cannot be used.

## `SPIClass`

`SPIClass` is this library's class (`<SPIClass.h>`), the common type of the
hardware bus (`CH32RVSPIClass`) and `SoftSPI`. It is not ArduinoCore-API's
`typedef HardwareSPI SPIClass`, so a forward declaration `class SPIClass;`
compiles. `SPI`'s type is `CH32RVSPIClass`, so `extern SPIClass SPI;` and
ESP32's `SPIClass spi(bus)` do not compile.

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
- **SoftSPILoopback** - the same with `SoftSPI`, on three pads you choose.

## Detailed example guides

- [SPILoopback](examples/SPILoopback/README.md): connections, operation, API use and adaptation notes.
- [SoftSPILoopback](examples/SoftSPILoopback/README.md): check a loopback on three pads of your choice with `SoftSPI`.
- [SpiFlashId](examples/SpiFlashId/README.md): Read an external SPI Flash JEDEC ID without erase or programming.
