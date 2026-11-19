# PiocIrNecRx

[日本語](README.ja.md)

NEC infrared remote frames received by the PIOC: `nec_rx.S` decodes what an IR receiver module (38 kHz, such as a
TSOP38238 or VS1838B) puts on IO0 into 32 bits, one whole frame a command, while the main core goes on. The address
and command go to Serial.

## What you need and wiring

A chip with a PIOC (X035 / X033, V205, H41x) and a 38 kHz IR receiver module. The module's output to IO0's pad (the
sketch's `IO0_PIN`: PA0 on CH32H417, PC7 on CH32X035, PC18 on CH32X033 (SWD's too), PB10 on CH32V205), its supply the chip's 3.3 V, GND shared. The
output is low while the carrier is on and high between (most modules pull their output up).

## Operation and expected result

Open Serial at 115200 baud and press a button on an NEC remote: `address 0x00 command 0x45` and the like. An
extended address (the second byte not the address's inverse) shows as 16 bits with `(extended)`; holding the button
gives `repeat`s.

## API and flow

- Time is counted in ticks: about 773 x d1 of the PIOC's cycles (the sketch picks d1 from `CH32RVPioc::clockHz()`
  for about 75 us). The line is looked at once a tick, so times are known within a tick.
- The thresholds sit halfway between NEC's times: a bit's space over 1125 us is a 1, a leader's space over 3375 us a
  frame (shorter: a repeat code), a leader's mark 6 ms at least. Each goes in d2 .. d4 as 256 - its ticks.
- `command()` starts waiting for a frame; `waitDone()` answers 0 for a frame (32 bits in d8 .. d11, the first bit in
  d8's bit 0), 1 for a repeat code, 2 for one broken off (a level held for 256 ticks).
- Without a frame the PIOC waits on, so when `waitDone()` times out the sketch begins it again to stop it.
- A low shorter than 6 ms is noise: it is passed over and the next leader waited for.

On H417 (PIOC at 100 MHz), X035 (48 MHz) and V205 (160 MHz), with the probe's spi-controller putting a receiver
module's output on MOSI (one bit a 562.5 us unit), a plain address (0x00 / 0x45), an extended one (0x1234 / 0x5A) and a
repeat code come out right (2026-11-20, pioc::test_pioc_nec_rx). Not yet tried with a real module and remote.
