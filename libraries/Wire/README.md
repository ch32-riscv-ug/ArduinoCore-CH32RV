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

## Things worth knowing

- **Pull-ups are yours.** The core configures the pads as open drain, which is
  what I2C needs, but there are no internal pull-ups strong enough. Fit 4.7k to
  3V3 on each line unless the module already has them. Without them every
  transfer times out.
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

## Detailed example guides

- [I2CScanner](examples/I2CScanner/README.md): connections, operation, API use and adaptation notes.
- [I2CRegisterRead](examples/I2CRegisterRead/README.md): Select and read registers, checking write errors and short replies.
- [I2CSlaveRegister](examples/I2CSlaveRegister/README.md): Expose a small register map with short interrupt callbacks.
- [I2CTimeoutRecovery](examples/I2CTimeoutRecovery/README.md): Distinguish NACK from timeout and explicitly request bus recovery.
