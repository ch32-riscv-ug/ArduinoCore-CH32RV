# PiocIrNec

[日本語](README.ja.md)

An NEC infrared remote's frames from the PIOC: `ir_nec.S` drives an IR LED on IO0 with the 38 kHz carrier (a third
high). Every second it sends address 0x00, command 0x45.

## What you need and wiring

A chip with a PIOC (X035 / X033, V205, H41x) and an IR LED, driven from IO0's pad (the sketch's `IO0_PIN`: PA0 on
CH32H417, PC7 on CH32X035, PB10 on CH32V205) through a transistor (NPN or an N-channel MOSFET); the pad alone gives too
little current for any range.

## Operation and expected result

An NEC receiver (a TV and the like) pointed at takes the command. A logic analyser shows the 9 ms leader, the 4.5 ms
space, 32 bits (address, its inverse, command, its inverse, LSB first) and the closing mark.

## API and flow

- d0 .. d3 are the frame's four bytes (an address above 0xFF: the extended address's high byte in d1).
- d4 / d5 are the carrier's high part and d6 / d7 its low part as wait turns; a period is (3 x d4 + 770 x d5) +
  (3 x d6 + 770 x d7) + 25 cycles, worked out from `CH32RVPioc::clockHz()`.
- Marks and spaces are counted in carrier periods (one routine, outputting or not, so both last alike).
- The PIOC is begun once and left running (`begin()` is a reset, IO0 floating meanwhile).

WireSkein's `ir` passes on H417, X035 (48 MHz) and V205 (2026-11-19, pioc::test_pioc_ir_nec: a plain and an extended
address, each command's inverse, the carrier at 37.99 .. 38.00 kHz, the marks' and spaces' lengths).
