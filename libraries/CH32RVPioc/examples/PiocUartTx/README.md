# PiocUartTx

[日本語](README.ja.md)

One more serial transmitter from the PIOC: `uart_tx.S` sends text on IO0 (8N1) while the main core goes on. Every
0.5 s it sends `PIOC says hello, millis() = ...` at 115200 baud.

## What you need and wiring

A chip with a PIOC (X035 / X033, V205, H41x). IO0's pad (the sketch's `IO0_PIN`: PA0 on CH32H417, PC7 on CH32X035, PC18 on CH32X033 (SWD's too),
PB10 on CH32V205) to a USB serial adapter's RX, GND shared.

## Operation and expected result

A serial terminal at 115200 baud shows a line every 0.5 s.

## API and flow

- The bytes go through the shared registers d8 .. d31, 24 at a time; d0 is their count, d1 / d2 the bit's wait.
  A bit is 3 x d1 + 770 x d2 + 11 of the PIOC's cycles, worked out from `CH32RVPioc::clockHz()` and the baud rate.
- `command()` starts the send and `waitDone()` waits for the last stop bit.
- No `loadSram()`: writing the SRAM holds the PIOC in reset, and IO0 floats meanwhile, losing the idle high. The PIOC
  is started once with `begin()` and left running.
- A bit is 14 cycles at least (about 3.4 Mbaud at 48 MHz) and about 200,000 at most (about 240 baud at 48 MHz).

Judged by WireSkein's `uart` (the bytes, 8N1, no framing error) at 9600, 115200 and 1 Mbaud on H417, X035 (48 MHz)
and V205 (2026-11-19, pioc::test_pioc_uart_tx).
