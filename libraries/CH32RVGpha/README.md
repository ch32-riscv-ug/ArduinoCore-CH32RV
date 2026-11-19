# CH32RVGpha

[日本語](README.ja.md)

The graphics DMA of CH32H415 / H416 / H417 (GPHA, CH32H417RM chapter 44): rectangles of pixels filled, copied with
their pixel format converted, and two blended, by the hardware.

```cpp
#include <CH32RVGpha.h>
CH32RVGpha gpha;
CH32RVGpha::Surface screen{frame, CH32RVGpha::RGB565, 320};   // pixels, format, pixels a line
gpha.fill(screen, 10, 20, 100, 50, 0xFF2040C0);               // x, y, w, h, ARGB
gpha.copy(icon, 0, 0, screen, 40, 40, 32, 32);                // 32 x 32 from icon's (0, 0) to screen's (40, 40)
gpha.blend(sprite, 0, 0, screen, 40, 40, screen, 40, 40, 32, 32, 200);   // sprite over screen, 200/255 overall
```

## API

| Function | What it does |
|---|---|
| `fill(to, x, y, w, h, argb)` | `to`'s rectangle set to `0xAARRGGBB` |
| `copy(from, fx, fy, to, tx, ty, w, h)` | `from`'s rectangle into `to`, its pixels converted to `to`'s format |
| `blend(fg, fx, fy, bg, bx, by, to, tx, ty, w, h, alpha)` | `fg` over `bg` into `to`, weighed by `fg`'s alpha times `alpha / 255` |
| `bitsOf(format)` | Bits a pixel |

Formats: ARGB8888, RGB888, RGB565, ARGB1555, ARGB4444 (in and out) and L8, AL44, AL88, L4, A8, A4 (in only). The
indexed L8, AL44, AL88 and L4 need a CLUT (colour table), which no call loads yet (A8 and A4 take the layer colour
registers, not settable yet either). Every
call waits for the transfer's end. A configuration the hardware refuses (CEIF) returns false, as does every call on a
series without the GPHA.

## Found on the bench (CH32H417, 2026-11-19)

- The fill colour (OCOLR) is ARGB8888 whatever the output format (the STM32 DMA2D takes the output's).
- Conversion to RGB565 keeps the high bits; expansion from RGB565 copies the high bits low (the manual's tables).
- A blend comes out up to 3 below the exact `(a x f + (255 - a) x b) / 255` per channel (opaque white over black: 0xFD).

## Test

`selfapi::test_gpha` (tests/instrumented/selfapi) checks fills and converting copies (both ways) exactly against the
CPU's reckoning and blends within -3 .. +1, with every pixel around the rectangles untouched: 6 of 6 on H417
(2026-11-19).
