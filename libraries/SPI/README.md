# SPI

SPI on the CH32's own peripheral, polled: a controller, or a peripheral (slave)
in a transaction that asks for one (below). Name the pins with
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
- No CS (an ordinary GPIO, as on `SPI`). A peripheral in a transaction that
  asks for one, as `SPI` (below): SCK is polled, so an interrupt longer than
  half the controller's clock period loses a bit. There is no `setRoute()`;
  `peripheral()` returns 0.

### SoftSPISlave (a slave framed by chip select, any pads)

`SoftSPISlave` is [CH32RVSPISlave](../CH32RVSPISlave/README.md)'s API on any
four pads: `begin(sck, miso, mosi, cs, mode, order)`, `queue()`, `poll()` /
`onTransfer()`, `transfer()`, `setFill()`, `missed()`, `overruns()`.

```cpp
#include <SPI.h>                  // or <SoftSPISlave.h>

SoftSPISlave slave;
static uint8_t tx[4] = {0xA5, 1, 2, 3}, rx[4];

void setup() {
  slave.begin(PC5, PC7, PC6, PC4);      // SCK, MISO, MOSI, CS
  slave.queue(tx, rx, sizeof rx);       // ready for the next chip select
}

void loop() {
  SoftSPISlave::Result r;
  if (slave.poll(r)) {                  // r.length whole bytes were clocked
    slave.queue(tx, rx, sizeof rx);
  }
}
```

- **Every bit is one interrupt.** CS takes a pin-change interrupt on both edges,
  SCK on the edge the mode samples on, where MOSI is read and MISO's next bit
  goes up. Each has to run within half a clock period of its edge, and the
  controller has to leave that much after CS falls and after the last clock
  before CS rises. A missed edge shows as a chip select whose clocks are not
  whole bytes, counted in `overruns()`. Not measured on hardware yet
  (2026-11-15); the bench test runs it at 50 kHz.
- **SCK and CS need different EXTI lines** (pin bits: PA1 and PB1 share line 1;
  `begin()` returns `false`), and those lines are the slave's. MISO may be -1
  (receive only), MOSI -1 (send only).
- MISO is driven only while CS is low, so other devices can share the bus.

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

## Buffer transfers and asynchronous transfers (DMA)

`transfer(buf, n)`, `transferBytes(out, in, n)` and `writeBytes(out, n)`
(ESP32's names) run, on the hardware bus, as one loop on the data register.
Out only (`writeBytes`, or `transferBytes` with a null `in`) the next byte goes
in at once, so the bytes leave back to back. Libraries that take a
`SPIClass *` (display drivers) get this too.

`transferAsync()` (arduino-pico's name) sends by DMA and returns at once; the
sketch runs on during the transfer:

```cpp
SPI.beginTransaction(SPISettings(36000000, MSBFIRST, SPI_MODE0));
digitalWrite(CS, LOW);
SPI.transferAsync(pixels, nullptr, sizeof pixels);   // false: it did not start
// ... draw the next lines ...
while (!SPI.finishedAsync()) {}                       // gives the DMA channels back at the end
digitalWrite(CS, HIGH);
SPI.endTransaction();
```

- A null `send` sends `0xFF`; a null `recv` drops what comes in. Leave both
  buffers alone, and the bus to this transfer, until `finishedAsync()` is
  `true`.
- `false` when the bus is closed, a peripheral or busy, or when another user
  (a `HardwareSerial` DMA buffer) holds one of this SPI's DMA channels (V20x's
  SPI1 shares them with USART3). The channels are held only from
  `transferAsync()` to the end.
- Over 65535 bytes it runs in parts, the next started by the
  `finishedAsync()` that sees one end.
- `abortAsync()` stops after the byte on the wire.
- `SoftSPI`'s `transferAsync()` is done before it returns (no DMA), so the
  same sketch runs with the SPI set to Software in the menu.
- The DMA code is linked only into a sketch that calls `transferAsync()`.

4096 bytes on V203 at 144 MHz (2026-11-18):

| SCK | `transfer()` a byte at a time | `writeBytes` | `transferBytes` (both ways) | `transferAsync` |
|---|---|---|---|---|
| 18 MHz | 3707 µs | 1823 µs | 3018 µs | 1823 µs |
| 36 MHz | 2802 µs | 915 µs | 2116 µs | 913 µs (both ways too) |

An LVGL flush can send with `transferAsync()` and wait with
`finishedAsync()` in `lv_display_set_flush_wait_cb()`.

## Hardware chip select (setHwCs)

As arduino-esp32's `SPI.setHwCs(true)`, the SPI drives its route's NSS pad itself:

```cpp
SPI.begin(PA5, PA6, PA7, PA4);   // ss is the route's NSS pad (left out: the route's NSS)
SPI.setHwCs(true);
SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));   // NSS goes low here
SPI.transfer(buf, n);
SPI.endTransaction();                                              // high once the last byte is out
```

- NSS is low from `beginTransaction()` to `endTransaction()` (SPE on and off). Transfers outside a transaction keep
  it low from `begin()`. ESP32 raises and lowers it around each transfer; that differs.
- Only the route's NSS pad (the variant's `CH32RV_SPIn_NSS_PADS`). With `ss` another pad, or on a series whose routes
  have no NSS (X033 / X035), it does not take and `hwCs()` is `false`.
- The code links only into a sketch that calls `setHwCs()`; the bus keeps a test and the pad's number (an SPI
  sketch on V003 grows by 24 B). Checked on V203's PA4 and V003's PC1 against the probe's spi-target (2026-11-18,
  spi::test_spi_hw_cs).

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
- **Peripheral (slave) mode as ArduinoCore-API defines it** (`SPIBusMode`;
  ArduinoCore-zephyr's SPI is the reference), on `SPI` and `SoftSPI`, and
  `SPI_HAS_PERIPHERAL_MODE` says so:

  ```cpp
  SPI.beginTransaction(SPISettings(0, MSBFIRST, SPI_MODE0, SPI_PERIPHERAL));
  uint8_t in = SPI.transfer(out);       // waits for the controller's clock
  SPI.endTransaction();                 // a controller again
  ```

  The clock is the controller's (the settings' frequency is not used), and
  `transfer()` waits for it with no timeout, as there. That API has no chip
  select: the bus is selected from `beginTransaction()` to `endTransaction()`
  and drives MISO all along, the bytes are framed by the `transfer()` calls,
  and the controller has to leave the time from one `transfer()` to the next
  between bytes (the hardware SPI takes each byte as `transfer()` gives it).
  For transfers framed by CS, queued ahead and moved by interrupts, use
  [CH32RVSPISlave](../CH32RVSPISlave/README.md) (the hardware SPI) or
  `SoftSPISlave` (any pads). Only a sketch that builds an `SPI_PERIPHERAL`
  `SPISettings` links the peripheral code (a patch to ArduinoCore-API's
  `SPISettings`, `tools/vendor/check_api_sync.py`); the buses keep only the
  test of a flag. Not measured on hardware yet (2026-11-15).
- **Names are `SPI`, `SPI1`, `SPI2`**, one per SPI the board enables. Using a
  name the board does not have fails to compile.
- The legacy `setBitOrder()` / `setDataMode()` / `setClockDivider()` are
  provided, because enough libraries still call them.

## Examples

- **SPILoopback** - one jumper from MOSI to MISO proves the clock, the data
  line and the framing in one go.
- **SoftSPILoopback** - the same with `SoftSPI`, on three pads you choose.
- **SpiAsyncDisplay** - an ST7789 display drawn while the previous lines go out by DMA (`transferAsync()`).

## Detailed example guides

- [SPILoopback](examples/SPILoopback/README.md): connections, operation, API use and adaptation notes.
- [SoftSPILoopback](examples/SoftSPILoopback/README.md): check a loopback on three pads of your choice with `SoftSPI`.
- [SpiFlashId](examples/SpiFlashId/README.md): Read an external SPI Flash JEDEC ID without erase or programming.
- [SpiAsyncDisplay](examples/SpiAsyncDisplay/README.md): an ST7789 display drawn while `transferAsync()` sends; LVGL's flush too.
