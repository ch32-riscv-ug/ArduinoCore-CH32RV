# PiocOneWire

[日本語](README.ja.md)

A 1-Wire master from the PIOC: `onewire.S` makes the reset, write and read slots on IO0 timed to the microsecond, so
interrupts on the main core cannot stretch them. Reads a DS18B20's temperature every second onto Serial.

## What you need and wiring

A chip with a PIOC (X035 / X033, V205, H41x) and a DS18B20 (other 1-Wire devices work with other commands). The
DS18B20's DQ to IO0's pad (the sketch's `IO0_PIN`: PA0 on CH32H417, PC7 on CH32X035, PB10 on CH32V205), a 4.7 kOhm
pull-up from DQ to 3.3 V, VDD at 3.3 V, GND shared (parasite power is not supported).

## Operation and expected result

Serial at 115200 baud shows the temperature every second, such as `23.5625 C`. With no device it says
`no device (no presence pulse)`; a scratchpad whose CRC does not match gives `CRC error`.

## API and flow

- `setOpenDrain(0)` makes the pad open drain: an output 0 pulls the line low, a 1 lets it go (the pull-up takes it
  high). Left push-pull as `begin()` makes it, the line cannot be let go.
- A microsecond is 3 x d1 + 4 of the PIOC's cycles, d1 worked out from `CH32RVPioc::clockHz()`.
- The command's value picks the work: 1 a reset (500 us low, let go, the line looked at 70 us later; cmd.out 0 with
  a presence pulse, 1 without), 2 writes d0 bytes from d8 .. d31, 3 reads d0 bytes (both LSB first, 24 at a time).
- A 1 is written as 6 us low and 64 let go, a 0 as 60 us low and 10 let go; a read slot is 6 us low, the line looked
  at 9 us after letting go, then 55 us.
- The sketch assumes one device on the bus (Skip ROM); more devices want a ROM search added.

Judged by WireSkein's `onewire` (standard speed) on a line with no device (the probe's pull-up) on H417, X035 and
V205 (2026-11-20, pioc::test_pioc_onewire): resets with no presence pulse, the bytes written and the reads' ff, every
slot and low within the standard. Measured on V205: reset 500 us, a 0's low 60.5 us, a 1's 6.5 us, slots 70 us. Not
yet tried with a real DS18B20.
