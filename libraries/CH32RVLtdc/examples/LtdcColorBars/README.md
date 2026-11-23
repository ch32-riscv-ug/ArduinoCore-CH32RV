# LtdcColorBars

[日本語](README.ja.md)

Shows eight colour bars (white, yellow, cyan, green, magenta, red, blue, black) on a 480 x 272 RGB panel (16-bit, RGB565) with the LTDC.

## What you need

- A CH32H417 board (H415 / H416 bond fewer LTDC pads: the sketch's pins do not all exist there)
- A 4.3-inch 480 x 272 16-bit RGB panel (DE mode)

## Wiring

| Signal | Pads |
|---|---|
| CLK / HSYNC / VSYNC / DE | PB14 / PA6 / PA4 / PE13 |
| R3-R7 | PB0, PA5, PC0, PB1, PD4 |
| G2-G7 | PD15, PE11, PB10, PB11, PC7, PB15 |
| B3-B7 | PD7, PE12, PD5, PA14, PB9 |

The panel's other colour lines (R0-R2, G0-G1, B0-B2) go to GND or as its datasheet says. Any pad of the variant's `CH32RV_LTDC_PADS` for the same role will do.

## What happens

It prints `LTDC at 10000000 Hz pixel clock` and the panel shows eight vertical bars.

## When it does not work

- `a pin is not the LTDC's for that role`: that pad is not the LTDC's for that role.
- The bars roll or flicker: check the panel's timing (sync, porches) and the pixel clock's edge (`clockInverted`).

## Before you run it

The output is on the hardware `Serial` at 115200 baud, 8N1. The generated `sketch.yaml` has the build profiles. See also the [build rules](../../../../docs/examples-build-rules.ja.md) and the [upload routes](../../../../docs/upload-routes.ja.md).
