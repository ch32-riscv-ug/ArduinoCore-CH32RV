# PiocWs2812

[日本語](README.ja.md)

WS2812 (NeoPixel) LEDs driven by the PIOC. While `ws2812.S` sends a frame, the main core goes on with its interrupts on
(the core's `rgbLedWrite()` sends by the CPU and holds interrupts for the frame). A rainbow runs along 30 LEDs.

## What you need and wiring

A chip with a PIOC (X035 / X033, V205, H41x) and WS2812 LEDs (a strip or one). IO0's pad (the sketch's `IO0_PIN`: PA0
on CH32H417, PC7 on CH32X035, PC18 on CH32X033 (SWD's too), PB10 on CH32V205) to the LEDs' DIN, their supply and GND shared. Add a level shifter
where 5 V LEDs do not take a 3.3 V signal.

## Operation and expected result

A slowly moving rainbow on the LEDs (at 1/8 brightness).

## API and flow

- The colours (G, R, B per LED) go into the PIOC's program SRAM at 0x400 by `loadSram(0x400, pixels, n)`. The PIOC
  starts again, so d0 .. d5 are written after it.
- d0 / d1 are the bytes to send, d2 .. d5 the turns of a 3-cycle loop, worked out from `CH32RVPioc::clockHz()` for a
  0's high (350 ns), a 1's high (800 ns), a bit (1.25 us) and a byte's last bit (the same period, the next byte's set-up in it); the program's cycle counts are at the top of `ws2812.S`.
- `command()` starts the frame and `waitDone()` waits for its end; 50 us or more low then latches it (300 us here).
- Up to the SRAM's 0x400 .. 0xFFF: 3072 bytes (1024 LEDs).

Judged by WireSkein's `rgb_led` (T0H, T1H, the bit period and the bytes) on frames of 10 and 100 LEDs on H417, X035 (48 MHz) and V205
(2026-11-19, pioc::test_pioc_ws2812).
