# PiocDmx

[日本語](README.ja.md)

A DMX512 controller from the PIOC: `dmx_tx.S` sends a universe of 32 channels on IO0 - the break, the mark after
it, the start code 0 and the channels at 250 kbit/s, 8N2 - 40 times a second, while the main core works out the next
values (a slow chase across the channels).

## What you need and wiring

A chip with a PIOC (X035 / X033, V205, H41x) and an RS-485 driver (a MAX485 or the like, DE and RE tied high for a
transmitter). IO0's pad (the sketch's `IO0_PIN`: PA0 on CH32H417, PC7 on CH32X035, PC18 on CH32X033 (SWD's too), PB10
on CH32V205) to the driver's DI; its A / B to the DMX line (XLR pin 3 / 2), GND shared, a 120 ohm terminator at the
line's far end.

## Operation and expected result

A DMX fixture or dimmer set to address 1 follows channel 1, and so on up to 32: each channel's level rises and falls
in about 4 s, one after another.

## API and flow

- The bytes go through the shared registers d8 .. d31, 24 at a time; d0 is their count, d1 / d2 the bit's wait (4 us
  worked out from `CH32RVPioc::clockHz()`), d5 bit 0 asks for the break (23 bits low: 92 us) and the mark after it (3
  bits: 12 us) before the bytes. The first chunk begins the packet with the start code; the rest follow it (the gap
  between slots may be up to a second).
- `command()` starts the send and `waitDone()` waits for the last stop bit. The PIOC is started once with `begin()`
  and left running, so the line stays high (the mark) between packets.
- The universe can be up to 512 channels: `CHANNELS` and the 25 ms pause set the refresh rate (512 channels take about
  23 ms at 250 kbit/s).

Judged by WireSkein's `dmx512` (three packets of the start code 0 and 30 slots each, exactly as sent) on H417, X035
and V205 (2026-11-23, pioc::test_pioc_dmx).
