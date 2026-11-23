/* The LCD-TFT controller (LTDC) of CH32H41x: an RGB panel driven from frame
 * buffers in RAM - the pixel clock, HSYNC, VSYNC, DE and up to 24 colour
 * lines, two layers blended over a background colour. No Arduino or
 * arduino-esp32 counterpart (arduino-esp32 leaves RGB panels to esp_lcd).
 *
 *     CH32RVLtdc lcd;
 *     CH32RVLtdcTiming t;                 // a 480 x 272 panel (the EVT's)
 *     t.width = 480; t.height = 272;
 *     t.hsync = 4; t.hbp = 43; t.hfp = 8;
 *     t.vsync = 4; t.vbp = 12; t.vfp = 8;
 *     t.pixelHz = 10000000;
 *     lcd.setSignalPins(PB14, PA6, PA4, PE13);      // CLK, HSYNC, VSYNC, DE
 *     const int r[8] = {-1, -1, -1, PB0, PA5, PC0, PB1, PD4};   // R0 .. R7, -1 unused
 *     ...
 *     lcd.setColorPins(r, g, b);
 *     lcd.begin(t);
 *     static uint16_t fb[480 * 272];                // RGB565
 *     lcd.layer(1, fb, CH32RV_LTDC_RGB565, 0, 0, 480, 272);
 *
 * The pixel clock is SYSCLK (the system PLL) / 1 .. 64; pixelClock() says
 * which it gave. Polarities: HSYNC and VSYNC active low, DE active high,
 * data on the rising edge by default (CH32RVLtdcTiming's flags).
 *
 * A pad is the variant's CH32RV_LTDC_PADS row for its role (one pad can
 * carry two roles - PB14 is CLK and G0 - so the role comes from where it is
 * passed). Unused colour lines: -1 (a 16-bit panel takes R3-R7, G2-G7,
 * B3-B7). Frame buffers stay valid while shown; the LTDC reads them by its
 * own DMA. With no layer of the sketch's on, the background still shows
 * (the LTDC sends no colour outside a layer's window: a transparent layer 1
 * over the whole area stands in, one RGB565 line of `width` pixels on the
 * heap, repeated). CH32V4x7's LTDC (its pixel clock from the Ethernet or
 * USBHS PLL) is not covered yet. */
#pragma once

#include <Arduino.h>

#if defined(CH32RV_LTDC_PADS) && \
    (defined(CH32RV_SERIES_CH32H415) || defined(CH32RV_SERIES_CH32H416) || defined(CH32RV_SERIES_CH32H417))
#define CH32RV_HAS_LTDC 1
#else
#define CH32RV_HAS_LTDC 0
#endif

enum CH32RVLtdcFormat : uint8_t {
    CH32RV_LTDC_ARGB8888 = 0,
    CH32RV_LTDC_RGB888,
    CH32RV_LTDC_RGB565,
    CH32RV_LTDC_ARGB1555,
    CH32RV_LTDC_ARGB4444,
    CH32RV_LTDC_L8,
    CH32RV_LTDC_AL44,
    CH32RV_LTDC_AL88,
};

struct CH32RVLtdcTiming {
    uint16_t width = 0, height = 0;     /* the active area, pixels and lines */
    uint16_t hsync = 1, hbp = 1, hfp = 1;
    uint16_t vsync = 1, vbp = 1, vfp = 1;
    uint32_t pixelHz = 10000000;
    bool hsyncHigh = false;             /* active levels */
    bool vsyncHigh = false;
    bool deHigh = true;
    bool clockInverted = false;         /* data on the falling edge */
};

class CH32RVLtdc {
public:
    bool setSignalPins(int clk, int hsync, int vsync, int de);
    bool setColorPins(const int r[8], const int g[8], const int b[8]);

    bool begin(const CH32RVLtdcTiming &timing);
    void end();

    /* Layer 1 or 2: the buffer's top-left at (x, y) of the active area,
     * w x h pixels; null hides the layer. Applied at the next frame. */
    bool layer(uint8_t n, const void *framebuffer, CH32RVLtdcFormat format, uint16_t x, uint16_t y, uint16_t w,
               uint16_t h, uint8_t alpha = 255);
    /* Under the layers: 0xRRGGBB. */
    void setBackground(uint32_t rgb);

    uint32_t pixelClock() const { return _pixel_hz; }
    /* The line being sent now (0 at the first active line's top, negative in the
     * vertical blanking before it). */
    int line() const;

private:
    bool pad(int pin, uint8_t role);

    int _signal[4] = {-1, -1, -1, -1};
    int _color[24] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
    bool _begun = false;
    uint32_t _pixel_hz = 0;
    uint16_t _vstart = 0;
    uint8_t _layers = 0;   /* bit n: the sketch's layer n is on */
    uint16_t _w = 0, _h = 0;
};
