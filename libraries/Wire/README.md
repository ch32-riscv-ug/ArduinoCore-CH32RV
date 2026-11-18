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

`Wire.begin()` without pins is the fallback: `Wire` is the first enabled I2C
that has pins of its own - the board menu's FIX pins, or the variant's default
route under AUTO - and `Wire1` the other one. That is number order on every
series but H417, whose I2C1 has no default route for the whole series (PB8 /
PB9 are SWD, PB6 / PB7 are missing on some packages): there `Wire` is I2C2 and
`Wire1` I2C1, which needs `begin(sda, scl)`.

`SDA` and `SCL` name `Wire`'s default route, as the datasheet defines it. Print them if you are unsure - they differ per series:

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
bit-banged controller (and target) on any two pads. It is a `TwoWire`, like `Wire`, so a
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
- `setClock()` is the clock it keeps, up to what the CPU and the line allow:
  each half period waits for a deadline, so the pin writes and SCL's rise come
  out of the wait. Measured on the bench (2026-11-15, the probe's weak pull-ups):
  20 / 50 kHz within 1 % everywhere; the ceiling is about 67 kHz on an X035
  (its open drain is emulated), 78 kHz on a V006, 102 kHz on a V003, 210 kHz on
  a V103, 373 kHz on a V205, 526 kHz on a V203 and 862 kHz on an H417.
  Above the ceiling it runs at the ceiling, never faster than asked.
  `getClock()` returns what was asked.
- **A target too.** `bus.begin(0x42)` (or `bus.begin(0x42, sda, scl, 0)`)
  answers at 0x42 - or a 10-bit address (`0x2A5 | WIRE_10BIT`), and address 0
  after `setGeneralCall(true)` - with Wire's target contract: `onReceive(count)` after the
  STOP (or at a repeated START, before `onRequest()` in a register read),
  `onRequest()` at a read's address, 0xFF past what it wrote, the head of a
  write longer than the buffer. A START (SDA falling, `attachInterrupt()`)
  starts one interrupt that follows the controller's clock by watching both
  lines until the STOP: SDA is sampled as SCL rises, SDA moving while SCL is
  high is a repeated START / STOP. What the next low phase does to SDA (the
  ACK, letting go, the next bit it sends) is decided in the high phase before
  it and written as soon as the fall is seen. Where a callback runs
  (`onRequest()` at a read's address, the `onReceive()` a repeated START
  owes) it **holds SCL low until done** - clock stretching, so the callbacks
  may take their time; the controller has to allow stretching. What has to be
  in time: the START's interrupt, by the end of the first bit's low phase
  (about 10 us at 100 kHz), and in each low phase seeing the fall plus one pin
  write before SCL rises; the bit loop runs from RAM for that (about 0.5 KB).
  Measured against a 100 kHz controller (2026-11-15, a 5.5 us low half): the
  write lands 2.0-3.7 us after the fall on a V003 at 24 MHz, 3.1-4.0 us on an
  X035 at 48 MHz; the tests pass on V003, X035 and V203. Estimated ceiling on
  X035 and V003: about 100 kHz, not 400 kHz. **The CPU stays in that
  interrupt while the bus is busy, other targets' transfers included**; an
  interrupt of the same or higher priority running then can lose a bit. SCL
  takes no interrupt, so SDA and SCL may share an EXTI line (pin bit); SDA's
  line is the target's (no `attachInterrupt()` of your own on it). Only a
  sketch calling `begin(address)` links the target and the EXTI handlers.
- **One role at a time.** Unlike `Wire`, a `SoftWire` begun as a target is
  not a controller as well: `endTransmission()` returns 4 and `requestFrom()`
  0 until `begin()` makes it a controller again (its own START would be
  followed by its own interrupt). It has no multi-master arbitration either.
- 10-bit addresses and SMBus PEC work as on `Wire` (below). A target with a
  10-bit address or the general call on follows the bus in a loop of its own,
  run from flash and linked only by a sketch that names `WIRE_10BIT` or calls
  `setGeneralCall()`; a plain 7-bit target keeps the RAM loop above. From
  flash it is slower on parts with flash wait states: test it up to 50 kHz.
- **A target left holding SDA** (cut off mid-byte) is clocked free before
  each START - up to 9 SCL pulses while SDA reads low - the bus clear `Wire`
  does after a failed transfer.
- There is no `setRoute()`: that is the hardware's. `peripheral()` returns 0,
  and `clearBus()` frees the bus the way Wire's does.
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
  data NACK, 1 for an over-long write, 4 for a bus that is not open or a
  transfer that lost the bus to another controller every time (below), 0 for
  success.
- **The buffer is 32 bytes**, as on AVR. Writing more truncates and reports 1.
  Raise it with `-DCH32RV_WIRE_BUFFER_SIZE=128`; both buffers grow, on every
  instance.
- **Slave mode is interrupt-driven**, on `Wire` and on `SoftWire` alike.
  `begin(address)` selects slave mode; `onReceive()` and `onRequest()`
  callbacks run in interrupt context.
- **`Wire` is a controller and a target at once.** After `Wire.begin(0x42)`,
  `beginTransmission()` / `endTransmission()` / `requestFrom()` still work: the
  transfer waits for the bus to be free (a controller talking to this target
  meanwhile is answered), takes it, and the bus goes back to listening. The
  bus's timeout bounds the wait. A message being built for `endTransmission()` is safe from
  `onRequest()` (the target's reply has a buffer of its own); the receive
  buffer is shared, so read what `requestFrom()` brought before a controller
  can write to this target - its `onReceive()` message replaces it.
- **Multi-master.** A controller transfer that loses arbitration - or meets
  another controller's START or STOP in the middle, or is addressed as a
  target before its own START got out - lets the bus go, waits for it to be
  free and starts again, up to `CH32RV_WIRE_ARBITRATION_RETRIES` (3) more
  times; then `endTransmission()` returns 4 (AVR's "other error") and
  `requestFrom()` 0. AVR, arduino-esp32 and STM32duino report the first loss
  as 4 and leave the retry to the sketch. A bus whose BUSY looks stale is reset
  only after both lines have read high for 100 us, so another controller's
  transfer is not taken for one.
- **10-bit addresses**: `Wire.beginTransmission(0x2A5 | WIRE_10BIT)`,
  `Wire.requestFrom(0x2A5 | WIRE_10BIT, n)`, a target at
  `Wire.begin(0x2A5 | WIRE_10BIT)`. On the wire: the header 11110 + bits 9:8 +
  W, the low byte, and for a read a repeated START with the header + R. A
  target's 10-bit read (the write of the address alone, turned round) does not
  call `onReceive(0)`. `WIRE_10BIT` is a constant defined beside the
  controller's 10-bit code, so naming it is what links that code; it is not a
  compile-time constant (no `constexpr`, no `case` label).
- **General call**: `Wire.setGeneralCall(true)` makes a target answer address
  0 as well; its bytes come to `onReceive()` like any write. A controller
  sends one with `beginTransmission(0)`.
- **SMBus PEC**: `Wire.setPEC(true)` appends the CRC-8 (x^8 + x^2 + x + 1) of
  the whole transaction - address bytes included - to a write ended with a
  STOP, and makes `requestFrom(address, n)` read the device's PEC byte after
  the n data bytes: on a mismatch it returns 0 and nothing is readable. A write
  ended with `endTransmission(false)` carries its share of the CRC into the
  read that follows (SMBus read byte / word). Computed in software, the same on
  `Wire` and `SoftWire`; only a sketch calling `setPEC()` links it.
  `TwoWire::pec(crc, data, len)` is the CRC for a target's own replies.
- **No DMA yet.** Transfers are at most the 32-byte buffer and are polled (the
  controller) or interrupt-driven (the target).
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
- **A target holding SDA low** (cut off mid-byte by a reset or a lost clock)
  hangs every transfer, and resetting this peripheral cannot help: the line is
  not ours. `Wire.clearBus()` clocks SCL until the slave lets go (up to 9
  pulses), puts a STOP on the bus, and returns `true` when both lines read high.
  A controller does it itself when a transfer has gone wrong (not a NACK) and,
  after the peripheral's reset, SDA reads low while SCL is high - the bus is
  already unusable then, and the I2C specification's bus clear is the way out
  (the pulses are no faster than `setClock()`, 100 kHz at the most). Call it
  yourself for a bus found held at start-up. Works before or
  after `begin()`; an open bus is reopened as it was. Lines are released with
  the internal pull-up and SCL is never driven high, so a stretching slave is
  waited for (`CH32RV_WIRE_CLEAR_STRETCH_US`, 1 ms per pulse).
- Clock: `setClock(100000)` for standard mode, anything higher selects fast
  mode with a 2:1 duty cycle. The slowest is what the 12-bit CCR holds (about
  12 kHz with a 100 MHz I2C clock, 2.9 kHz at 24 MHz). `setClock()` returns
  `bool`, as arduino-esp32's: `false`, and the clock kept, for 0 and for a clock
  slower than that (the bus would run faster than asked); a clock faster than
  the peripheral makes runs at the fastest it makes. SoftWire refuses only 0.
  The bus never runs faster than the clock set.
  A controller takes a new clock at its next transfer. A controller counts SCL's
  high from when it sees it high, so slow rises (weak pull-ups) lengthen the
  period: 330-394 kHz for 400 kHz on the bench's probe pull-ups alone.
  The reference manuals give standard (100 kHz) and fast (400 kHz) mode. A
  faster clock is taken as asked - it can be set - but whether a part and its
  lines run it is not promised: on the bench (2026-11-15) 1 MHz asked ran at
  0.55-0.96 MHz on every part, and 3.4 MHz asked made every transfer time out
  (5) on V006, V103, V203, V205, X035 and H417 (V003 ran at 2 MHz). The
  peripheral clock is assumed to be `F_CPU`.
  Changes to system or APB clock settings must preserve or update this assumption.
- **H417's hardware I2C can put out an out-of-spec clock on weakly pulled-up
  lines.** On the bench (2026-11-14, only the probe's 45 kΩ pull-ups, 20-100 kHz)
  about one transaction in ten carried an SCL high of only 20-80 ns (the lows
  around it normal, mostly on bits sending a 1; I2C's FREQ and TRISE do not
  change it). A receiver may count it as an extra clock and take the address or
  the data wrong (the DUT then returns 2, 3 or 5). H41x pads have no internal
  pull-up while open-drain, so fit 2.2-4.7 kΩ external pull-ups. Whether those
  make the short highs go away is not yet measured. SoftWire runs on the same
  lines.

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
