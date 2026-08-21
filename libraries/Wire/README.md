# Wire (I2C)

Master-mode I2C on the CH32's own peripheral. The pins come from the variant,
so `Wire.begin()` takes no arguments.

```cpp
#include <Wire.h>

void setup() {
  Wire.begin();
  Wire.beginTransmission(0x3C);
  Wire.write(0x00);
  if (Wire.endTransmission() == 0) {
    // the device acknowledged
  }
}
```

## Which pins

`SDA` and `SCL` name the first bus's default route, as the datasheet defines
it. Print them if you are unsure - they differ per series:

```cpp
Serial.print(SCL); Serial.print(' '); Serial.println(SDA);
```

**The default route is not bonded on every package.** On CH32X033/X035 it
reaches PA10/PA11, which only two of the seven part numbers bring out. Move the
bus when your board does not have it:

```cpp
Wire.setRoute(2);                 // route number from the datasheet
Wire.setPins(PC16, PC17);         // or name the pads
```

Both return `false` and change nothing if the route does not exist. `setPins()`
also refuses SCL and SDA that belong to different routes - the hardware moves
the whole peripheral at once, and several X035 routes swap the two signals over
the same pair of pads, so the order matters and is checked.

Calling either after `begin()` reopens the bus on the new pins and returns the
old pads to inputs.

## Things worth knowing

- **Pull-ups are yours.** The core configures the pads as open drain, which is
  what I2C needs, but there are no internal pull-ups strong enough. Fit 4.7k to
  3V3 on each line unless the module already has them. Without them every
  transfer times out.
- **Every wait is bounded.** `CH32_WIRE_TIMEOUT_US` (25 ms by default) caps
  each step, so a stuck bus returns an error instead of hanging the sketch.
  `endTransmission()` returns 5 for a timeout, 2 for an address NACK, 3 for a
  data NACK, 1 for an over-long write, 0 for success.
- **The buffer is 32 bytes**, as on AVR. Writing more truncates and reports 1.
  Raise it with `-DCH32_WIRE_BUFFER_SIZE=128`; both buffers grow, on every
  instance.
- **Slave mode is not implemented.** `begin(address)`, `onReceive()` and
  `onRequest()` are accepted and do nothing rather than half-working.
- **A second bus is `Wire1`**, where the part has one - bus order, as elsewhere
  in the Arduino ecosystem, not the peripheral's number.
- Clock: `setClock(100000)` for standard mode, anything higher selects fast
  mode with a 2:1 duty cycle. The peripheral clock is assumed to be `F_CPU`,
  which holds while the core runs from HSI with both APB prescalers at /1.

## Examples

- **I2CScanner** - list the devices that answer, and say so plainly when the
  bus times out because the pull-ups are missing.
