# CH32RVSPISlave

[日本語](README.ja.md)

The SPI as a slave (peripheral): another chip is the master and drives the clock. A transfer is one chip select: from CS falling to CS rising, the slave sends the bytes you queued and keeps the bytes the master sent.

```cpp
#include <CH32RVSPISlave.h>

CH32RVSPISlave slave;
static uint8_t tx[4] = {0xA5, 0x01, 0x02, 0x03};
static uint8_t rx[4];

void setup() {
  slave.begin(PA5, PA6, PA7, PA4);      // SCK, MISO, MOSI, CS (any pin)
  slave.queue(tx, rx, sizeof rx);       // ready for the next chip select
}

void loop() {
  CH32RVSPISlave::Result r;
  if (slave.poll(r)) {                  // a chip select has ended
    // r.length bytes were clocked; rx[0 .. min(r.length, 4) - 1] holds them
    slave.queue(tx, rx, sizeof rx);     // ready for the next one
  }
}
```

## How a transfer goes

- **Queue first.** `queue(tx, rx, size)` arms one transfer; up to four wait their turn. The first byte of `tx` is in the SPI before CS falls.
- **The master decides the length.** When it clocks more than `size` bytes, the slave sends the fill byte (`setFill()`, 0xFF by default) after `tx` and keeps only `size` bytes; `Result.length` still counts every byte.
- **Nothing queued:** the slave sends the fill byte, keeps nothing, and counts the transfer in `missed()`.
- **A CS pulse with no clock** uses nothing up.
- **MISO is released** (a floating input) while CS is high, so several slaves can share the bus.

## Timing

CS is an ordinary pin with an interrupt, not the SPI's hardware NSS pad, and each byte is moved by an interrupt. So the master has to give the slave a little time:

- after CS falls, before the first clock edge;
- after CS rises, before the next CS falls;
- per byte: the interrupt has to run before the next byte is complete. A byte lost this way is counted in `overruns()`.

Measured 2026-10-28 with [SPISlaveSelfTest](examples/SPISlaveSelfTest/README.md) (the chip clocks its own pins from GPIO): modes 0 to 3 and LSB first pass on CH32V003 (24 MHz), V103, V307, X035 and L103 with a clock of about 100 to 200 kHz, and on CH32V003 with 2 µs (plus a `digitalWrite()`) between CS and the clock. The upper clock with a hardware master is not measured yet.

## API

| Call | Meaning |
|---|---|
| `begin(sck, miso, mosi, cs, mode = SPI_MODE0, order = MSBFIRST)` | SCK / MISO / MOSI have to be one route of an SPI, as for `SPI.begin(sck, miso, mosi)`. False otherwise, or when that SPI is in use |
| `end()` | Drops what is queued, gives back the pins and the SPI |
| `queue(tx, rx, size)` | Arm one transfer. `tx` nullptr: the fill byte; `rx` nullptr: keep nothing. False when four are waiting (finished ones not yet taken count) |
| `transfer(tx, rx, size, timeout_ms = 0)` | `queue()` and wait for it; the bytes clocked, 0 on a timeout. Only with nothing else queued or waiting |
| `poll(result)` | The oldest finished transfer: `rx`, `length` (clocked), `size` |
| `onTransfer(callback, arg)` | Or each finished transfer to the callback, run by `deliverEvents()` from `loop()` |
| `setFill(byte)` | Sent after `tx` and when nothing is queued |
| `numTransactionsInFlight()` / `available()` | Queued and not finished / finished and not taken |
| `missed()` / `overruns()` | Chip selects with nothing queued / bytes lost to a late interrupt |

An SPI used by `SPI` (the bus object) cannot be a slave at the same time. On a board with only one SPI, do not open both.

## Families

Every family with an SPI. Bit order is reversed in software for LSB first: on CH32V003 and CH32X035 a slave with the SPI's own LSB-first setting transmits the first bit of each byte eight times.

## Examples

- [SPISlaveSelfTest](examples/SPISlaveSelfTest/README.md): checks the slave with no master and no wiring.
