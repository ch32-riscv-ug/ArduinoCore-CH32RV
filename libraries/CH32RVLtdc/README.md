# CH32RVLtdc

[日本語](README.ja.md)

The LCD-TFT controller (LTDC) of CH32H41x: an RGB panel driven from frame buffers in RAM - pixel clock, HSYNC, VSYNC, DE and up to 24 colour lines, two layers blended over a background colour. No Arduino or arduino-esp32 counterpart (arduino-esp32 leaves RGB panels to esp_lcd).

```cpp
#include <CH32RVLtdc.h>

CH32RVLtdc lcd;

void setup() {
  CH32RVLtdcTiming t;                              // a 480 x 272 panel
  t.width = 480; t.height = 272;
  t.hsync = 4; t.hbp = 43; t.hfp = 8;
  t.vsync = 4; t.vbp = 12; t.vfp = 8;
  t.pixelHz = 10000000;
  const int r[8] = {-1, -1, -1, PB0, PA5, PC0, PB1, PD4};     // R0 .. R7, -1: unused
  const int g[8] = {-1, -1, PD15, PE11, PB10, PB11, PC7, PB15};
  const int b[8] = {-1, -1, -1, PD7, PE12, PD5, PA14, PB9};
  lcd.setSignalPins(PB14, PA6, PA4, PE13);          // CLK, HSYNC, VSYNC, DE
  lcd.setColorPins(r, g, b);
  lcd.begin(t);
  uint16_t *fb = (uint16_t *)malloc(480 * 272 * 2); // RGB565
  lcd.layer(1, fb, CH32RVLtdcFormat::Rgb565, 0, 0, 480, 272);
}
```

| Function | Meaning |
|---|---|
| `setSignalPins(clk, hsync, vsync, de)` | The signal pads (before `begin()`) |
| `setColorPins(r[8], g[8], b[8])` | The colour pads, bit 0 first; -1 for a line not used |
| `begin(timing)` / `end()` | Starts the panel with `CH32RVLtdcTiming`; stops it, pads back to inputs |
| `layer(n, fb, format, x, y, w, h, alpha = 255)` | Layer 1 or 2: the buffer at (x, y), w x h pixels; null hides it. Applied at the next frame |
| `setBackground(0xRRGGBB)` | The colour under the layers |
| `pixelClock()` | The pixel clock it gave (H41x: SYSCLK / 1 .. 64; V4x7: 500 MHz / 1 .. 63) |
| `line()` | The line being sent now (negative in the vertical blanking) |

`CH32RVLtdcTiming`: `width` / `height`, `hsync` / `hbp` / `hfp`, `vsync` / `vbp` / `vfp`, `pixelHz`, and the polarities `hsyncHigh`, `vsyncHigh` (false: active low), `deHigh` (true), `clockInverted` (false: data on the rising edge). Formats: ARGB8888, RGB888, RGB565, ARGB1555, ARGB4444, L8, AL44, AL88 (`CH32RV_LTDC_*`).

- A pad must be the variant's `CH32RV_LTDC_PADS` row for its role. One pad can carry two roles (PB14 is CLK and G0): the role comes from where it is passed.
- Frame buffers must stay valid while shown: the LTDC reads them by its own DMA. A 480 x 272 RGB565 frame is 255 KiB.
- With no layer of the sketch's on, the background still shows: a transparent layer 1 over the whole area stands in (the LTDC sends no colour outside a layer's window), one RGB565 line of `width` pixels on the heap.
- CH32H415 / H416 bond fewer LTDC pads. CH32V4x7's LTDC has one layer (`layer(2, ...)` is false) and its pixel clock from the Ethernet PLL (HSE x 20, 500 MHz) / 1 .. 63, which `begin()` starts and leaves on (the Ethernet MAC uses it too); built only (no bench lines are its HSYNC and VSYNC).

## Examples

- [LtdcColorBars](examples/LtdcColorBars/README.md): eight colour bars on a 480 x 272 panel.

## Tests

periph::test_ltdc: HSYNC, VSYNC, DE and one R and one G line captured; WireSkein checks the sync periods and duties, DE against the colour line, and the other colour staying low - the background only, a colour layer, and a transparent layer over the background. Passes on H417 (2026-11-23). No panel connected; H415 / H416 built only.
