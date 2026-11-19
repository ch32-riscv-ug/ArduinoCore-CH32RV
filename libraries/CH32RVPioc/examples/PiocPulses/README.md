# PiocPulses

[日本語](README.ja.md)

A PIOC program written in the sketch's `pulses.S` puts n pulses on IO0 for each command n and answers 0x5A.

## What you need and wiring

A part with a PIOC (X035 / X033, V205, H41x). IO0's pad (the sketch's `IO0_PIN`: PA0 on CH32H417, PC7 on CH32X035, PC18 on CH32X033 (SWD's too), PB10 on CH32V205) to a logic analyser or a scope.

## Operation and expected result

`Serial` prints `1 pulses -> 0x5A, commands so far 1` and so on every half second; IO0 shows 1 to 8 pulses. Each is
high for 4 PIOC cycles in a period of 9 (40 ns high, 90 ns period at H417's 100 MHz; checked with the probe
2026-11-19).

## API and flow

`PIOC_IMAGE(pulses)` declares the `.S`'s image; `begin(PIOC_IMAGE_OF(pulses), IO0_PIN)` copies it into the PIOC's
SRAM and runs it. `command(n)` writes cmd.in; the program goes on from `WAITB WB_DATA_MW_SR_1`, makes n pulses,
writes 0x5A to cmd.out and sets `SB_INT_REQ`, which `waitDone()` waits for before reading cmd.out. d1 counts the
commands the program has seen.

## Changing it

More NOPs in `pulses.S` make longer pulses. For IO1 too, pass its pad as `begin()`'s third argument and move
`SB_PORT_OUT1` in the program.

## Related documents

- [Sketch](PiocPulses.ino), [the PIOC program](pulses.S)
- [CH32RVPioc](../../README.md)
