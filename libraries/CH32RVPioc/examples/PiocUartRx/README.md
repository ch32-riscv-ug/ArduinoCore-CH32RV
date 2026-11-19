# PiocUartRx

[日本語](README.ja.md)

One more serial receiver from the PIOC: `uart_rx.S` takes 8N1 bytes on IO0 into the shared registers, and the sketch
prints them on `Serial` eight at a time (after 2 s without them, whatever came).

## What you need and wiring

A chip with a PIOC (X035 / X033, V205, H41x). The sender's TX (a USB serial adapter or the like, at 3.3 V) to IO0's
pad (the sketch's `IO0_PIN`: PA0 on CH32H417, PC7 on CH32X035, PC18 on CH32X033 (SWD's too), PB10 on CH32V205), GND shared; the rate is the
sketch's `BAUD` (115200).

## Operation and expected result

Every eight characters sent from the adapter print as `PIOC got: ` and those eight.

## API and flow

- d0 is the bytes to take (1 .. 24), d1 / d2 the bit's wait and d3 / d4 the wait from the start bit's falling edge to
  bit 0's middle; a bit is 3 x d1 + 770 x d2 + 11 of the PIOC's cycles, worked out from `CH32RVPioc::clockHz()`.
- The program waits for the line high, then for a start bit, and reads IO0 in one cycle (`BG2F`) in each bit's middle.
  The bytes go into d8 .. d31 and d7 counts them; when d0 are in, `waitDone()` returns 0.
- On a time-out before that, read d7 and d8 .. and `begin()` again to stop the wait.
- The host does not read while the 24 are coming, so taking more needs a pause in the sender.

On H417, X035 (48 MHz) and V205 the 24 bytes the probe's UART sent at 9600, 115200 and 1 Mbaud came in without one
wrong (2026-11-19, pioc::test_pioc_uart_rx).
