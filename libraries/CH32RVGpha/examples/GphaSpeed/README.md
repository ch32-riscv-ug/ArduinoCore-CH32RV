# GphaSpeed

[日本語](README.ja.md)

The GPHA (H41x's graphics DMA) against the CPU: a 160 x 120 RGB565 frame filled, and a 64 x 64 ARGB8888 picture copied
into it converted to RGB565, by each; the times go to `Serial`.

## What you need and wiring

A chip with the GPHA (H415 / H416 / H417). No wiring.

## Operation and expected result

Two lines: `fill 160x120 RGB565: GPHA ... us, CPU ... us` and `copy 64x64 ARGB8888 -> RGB565: ...`. On H417 (V5F at
400 MHz) the fill took 194 us by the GPHA and 192 us by the CPU, the converting copy 45 us against 247 us (2026-11-19):
the CPU is fast, so the gain is in conversion and blending rather than plain fills.

## API and flow

A `Surface` is a buffer (pixels, format, pixels a line); `fill()`, `copy()` and `blend()` work on its rectangles in
hardware - for an LCD frame buffer (LTDC) or a frame built up before it goes to an SPI LCD.
