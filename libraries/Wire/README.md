# Wire (I2C)

Master/slave I2C on the CH32's own peripheral. Name the pins as on an ESP32:
`Wire.begin(sda, scl)` or `Wire.begin(sda, scl, 400000)`.
Libraries take the bus as `TwoWire *` (`&Wire`), and a `SoftWire` passes there too.

```cpp
#include <Wire.h>

void setup() {
  Wire.begin(SDA, SCL);           // the pads you wired
  Wire.beginTransmission(0x3C);
  Wire.write(0x00);
  if (Wire.endTransmission() == 0) {
    // the device acknowledged
  }
}
```

## Which pins

The bus takes whichever free I2C has the pins as a route. A name is not tied
to an I2C number: `Wire` on I2C2's pins and `Wire1` on I2C1's is fine.
`peripheral()` tells which I2C a bus landed on (0 while closed).

`Wire.begin()` without pins is the fallback: the board's enabled I2Cs in
number order, `Wire` the first and `Wire1` the second, on their default pins -
the board menu's FIX pins, or the variant's default route under AUTO.

`SDA` and `SCL` name the first I2C's default route, as the datasheet defines
it. Print them if you are unsure - they differ per series:

```cpp
Serial.print(SCL); Serial.print(' '); Serial.println(SDA);
```

**The default route is not bonded on every package.** On CH32X033/X035 it
reaches PA10/PA11, which some packages do not bring out. Move the
bus when your board does not have it:

```cpp
Wire.setRoute(2);                 // route number from the datasheet
Wire.setPins(PC17, PC16);         // or name the pads: SDA, SCL
```

Both return `false` and change nothing if the route does not exist. `setPins()`
takes SDA first, as on an ESP32, and also refuses SDA and SCL that belong to different routes - the hardware moves
the whole peripheral at once, and several X035 routes swap the two signals over
the same pair of pads, so the order matters and is checked.

Calling either after `begin()` reopens the bus on the new pins and returns the
old pads to inputs.

## SoftWire (I2C on any two pads)

The hardware I2C reaches only the pads its routes name, and on the small parts
those are often not bonded out, or already carry something else. `SoftWire` is a
bit-banged controller on any two pads. It is a `TwoWire`, like `Wire`, so a
library that takes `TwoWire *` takes it unchanged. It comes with the same
`<Wire.h>` (or `<SoftWire.h>`).

```cpp
#include <Wire.h>

SoftWire bus(PA1, PA2);           // SDA, SCL
Adafruit_Something device(&bus);
```

- **The class says software or hardware.** `Wire.begin(sda, scl)` takes a
  hardware I2C only, and returns `false` for pins no route has; it never falls
  back to software behind the sketch's back.
- Both pads are `OUTPUT_OPENDRAIN` (emulated as input / output-low on X033 /
  X035, whose GPIO has no open drain). **The bus still needs pull-ups.**
- Clock stretching is waited for. `endTransmission()`'s codes and the timeout
  API are Wire's.
- `setClock()` sets a **floor** on the half period, not an achieved frequency;
  `getClock()` returns what was asked.
- **No target mode.** `begin(address)` opens a controller, and `onReceive()` /
  `onRequest()` do nothing. There is no `setRoute()`: that is the
  hardware's. `peripheral()` returns 0, and `clearBus()` frees the bus the way
  Wire's does.
- The buffers are Wire's `CH32RV_WIRE_BUFFER_SIZE` (32 by default).
- `endTransmission()` on a bus that is not open returns 4, as on `Wire` (it
  does not `begin()` by itself).

### Making Wire software from the board menu

Choose **Software** under **I2C1** (I2C2) in the board menu and the name that
I2C backs (`Wire`, `Wire1`) is a `SoftWire`. `begin()` uses that I2C's default
route pins and `begin(sda, scl)` takes any pads; no hardware I2C code is linked.
It lets a library that uses the global `Wire` run on a board whose hardware I2C
cannot be used. Hardware-only calls such as `Wire.setRoute()` then fail to
compile.

## Things worth knowing

- **Pull-ups are yours.** The core configures the pads as open drain, which is
  what I2C needs, but there are no internal pull-ups strong enough. Fit 4.7k to
  3V3 on each line unless the module already has them. Without them every
  transfer times out.
- **Some X035s may not do I2C.** The datasheet says a CH32X035 / X033 whose lot
  number has 0 as its 5th-from-last digit has no I2C (nor ADC channels 3, 7, 11
  and 15). On a C8T6 of such a lot, I2C on PA10 / PA11 kept SDA high, no target
  could ACK, and `endTransmission()` returned 2 (address NACK); on an F8U6 of
  such a lot, I2C on PC16 / PC17 (route 2) worked. Check the marking first.
- **Every wait is bounded.** `CH32RV_WIRE_TIMEOUT_US` (25 ms by default) caps
  each step, so a stuck bus returns an error instead of hanging the sketch.
  `endTransmission()` returns 5 for a timeout, 2 for an address NACK, 3 for a
  data NACK, 1 for an over-long write, 0 for success.
- **The buffer is 32 bytes**, as on AVR. Writing more truncates and reports 1.
  Raise it with `-DCH32RV_WIRE_BUFFER_SIZE=128`; both buffers grow, on every
  instance.
- **Slave mode is interrupt-driven.** `begin(address)` selects slave mode;
  `onReceive()` and `onRequest()` callbacks run in interrupt context. An instance
  cannot act as master and slave simultaneously. Multi-master arbitration and
  general-call responses are unsupported.
- **Slave mode is begun on the bus object** (`Wire.begin(0x42)`), not through
  a `TwoWire *`: `begin(address)` belongs to `Wire`'s and `SoftWire`'s classes,
  not to `TwoWire`, so a sketch that never calls it links none of the slave's
  interrupt handling (a controller-only `Wire` sketch is 3364 B on V003, 644 B
  less than with it). `TwoWire` is not derived from ArduinoCore-API's
  `HardwareI2C` for the same reason.
- **Names are `Wire`, `Wire1`**, one per I2C the board enables (not set to
  Disable in the menu). Using a name the board does not have fails to compile.
- Timeout: a wait gives up after 25 ms and `endTransmission()` returns 5. That
  is **on by default**, unlike AVR, where a stuck bus hangs the sketch until
  the sketch asks for a timeout. `setWireTimeout(us)` changes it and
  `setWireTimeout(0)` turns it off; `getWireTimeoutFlag()` is sticky until
  `clearWireTimeoutFlag()`. The peripheral is reset after a timeout either way,
  so `reset_with_timeout` is accepted and does not change anything.
- **A slave holding SDA low** (cut off mid-byte by a reset or a lost clock)
  hangs every transfer, and resetting this peripheral cannot help: the line is
  not ours. `Wire.clearBus()` clocks SCL until the slave lets go (up to 9
  pulses), puts a STOP on the bus, and returns `true` when both lines read high.
  It is never done automatically - the pulses reach every device on the bus -
  so call it when a transfer times out and you know it is safe. Works before or
  after `begin()`; an open bus is reopened as it was. Lines are released with
  the internal pull-up and SCL is never driven high, so a stretching slave is
  waited for (`CH32RV_WIRE_CLEAR_STRETCH_US`, 1 ms per pulse).
- Clock: `setClock(100000)` for standard mode, anything higher selects fast
  mode with a 2:1 duty cycle. The peripheral clock is assumed to be `F_CPU`.
  Changes to system or APB clock settings must preserve or update this assumption.

## Examples

- **I2CScanner** - list the devices that answer, and say so plainly when the
  bus times out because the pull-ups are missing.
- **SoftI2CScanner** - the same with `SoftWire`, on two pads you choose.

## Detailed example guides

- [I2CScanner](examples/I2CScanner/README.md): connections, operation, API use and adaptation notes.
- [SoftI2CScanner](examples/SoftI2CScanner/README.md): scan two pads of your choice with `SoftWire`.
- [I2CRegisterRead](examples/I2CRegisterRead/README.md): Select and read registers, checking write errors and short replies.
- [I2CSlaveRegister](examples/I2CSlaveRegister/README.md): Expose a small register map with short interrupt callbacks.
- [I2CTimeoutRecovery](examples/I2CTimeoutRecovery/README.md): Distinguish NACK from timeout and explicitly request bus recovery.
