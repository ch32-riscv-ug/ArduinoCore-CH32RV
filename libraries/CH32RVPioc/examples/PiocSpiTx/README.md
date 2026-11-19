# PiocSpiTx

[日本語](README.ja.md)

One more SPI transmitter (mode 0, MSB first) from the PIOC: `spi_tx.S` drives SCK on IO0 and MOSI on IO1 while the
main core goes on. CS is an ordinary pin the sketch drives. Every 100 ms it sends a 16-bit counter at 1 MHz (to two
chained 74HC595s, for one).

## What you need and wiring

A chip with a PIOC (X035 / X033, V205, H41x).

| Chip | SCK (IO0) | MOSI (IO1) | CS |
|---|---|---|---|
| CH32H417 | PA0 | PC0 | PA1 |
| CH32X035 | PC7 | PC19 | PA1 |
| CH32V205 | PB10 | PB11 | PB2 |

To the receiver's clock, data and CS (the latch on a 74HC595): a shift register, an LED driver, a display. GND shared.
A logic analyzer shows it without a receiver.

## Operation and expected result

Every 100 ms CS goes low and two bytes (the counter's high, then low byte) go out at 1 MHz. On a 74HC595 the outputs
count up.

## API and flow

- The bytes go through the shared registers d8 .. d31, 24 at a time; d0 is their count, d1 the half period's wait.
  A half period is 3 x d1 + 6 of the PIOC's cycles, worked out from `CH32RVPioc::clockHz()` and the rate (1 .. 256).
- Every bit: MOSI set while SCK is low, half a period, SCK high (the receiver samples), half a period, SCK low.
- `command()` starts the send and `waitDone()` waits for the last bit. More than 24 bytes go on in further commands
  with CS still low.
- The PIOC is started once with `begin()` and left running (SCK stays low; the pads float during `loadSram()`).
- SCK runs from the PIOC's clock / 18 (1 turn) down to clock / 1548 (256): about 2.7 MHz .. 31 kHz at 48 MHz (X035),
  5.6 MHz .. 65 kHz at 100 MHz (H417) and 8.9 MHz .. 103 kHz at 160 MHz (V205). Slower than that, the sketch clamps to 256.
- No receive (MISO): the PIOC has two pads.

Judged by WireSkein's `spi` (the bytes within CS, mode 0, MSB first) at 100 kHz and 1 MHz on H417 (PIOC at 100 MHz)
and V205 (160 MHz) (2026-11-20, pioc::test_pioc_spi_tx). The SCK measured 952 kHz / 99.5 kHz and 1.00 MHz / 103.6 kHz,
within 3 % of the formula above (V205's 100 kHz is its 103 kHz floor). Not yet on X035: its bench has no line on IO1 (PC19).
