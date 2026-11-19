/* The graphics DMA of CH32H415 / H416 / H417 (GPHA, CH32H417RM chapter 44):
 * rectangles of pixels moved by the hardware - filled with a colour, copied
 * with their pixel format converted, or two blended into a third.
 *
 *   CH32RVGpha gpha;
 *   CH32RVGpha::Surface screen{frame, CH32RVGpha::RGB565, 320};   // pixels, format, pixels a line
 *   gpha.fill(screen, 10, 20, 100, 50, 0xFF2040C0);               // x, y, w, h, ARGB
 *   gpha.copy(icon, screen, 0, 0, 32, 32);                         // from (0, 0) of icon to (0, 0) of screen
 *
 * A Surface is a buffer of `pitch` pixels a line in one of the formats
 * below. Colours given to fill() are 0xAARRGGBB; a blend can come out up to 3
 * below the exact mix per channel (the hardware's rounding). Each call waits for the end
 * of the transfer (the CPU idles meanwhile; the hardware is still faster than
 * the CPU's own loop). Output formats are ARGB8888 .. ARGB4444; the L / A
 * formats are inputs only. Where the series has no GPHA (CH32RV_GPHA_BASE)
 * every call returns false. */
#pragma once

#include <Arduino.h>
#include <stdint.h>

class CH32RVGpha {
public:
    enum Format : uint8_t {
        ARGB8888 = 0,
        RGB888 = 1,
        RGB565 = 2,
        ARGB1555 = 3,
        ARGB4444 = 4,
        L8 = 5,
        AL44 = 6,
        AL88 = 7,
        L4 = 8,
        A8 = 9,
        A4 = 10,
    };
    struct Surface {
        void *pixels;
        Format format;
        uint16_t pitch;        /* pixels a line of the buffer */
    };

    /* The w x h rectangle at (x, y) of `to` set to argb (0xAARRGGBB). */
    bool fill(const Surface &to, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t argb);
    /* The w x h rectangle at (fx, fy) of `from` into `to` at (tx, ty), its
     * pixels converted to `to`'s format. */
    bool copy(const Surface &from, uint16_t fx, uint16_t fy, const Surface &to, uint16_t tx, uint16_t ty, uint16_t w,
              uint16_t h);
    /* fg over bg into `to` (each its own rectangle's top left; w x h):
     * fg's alpha times `alpha` / 255 weighs fg against bg. */
    bool blend(const Surface &fg, uint16_t fx, uint16_t fy, const Surface &bg, uint16_t bx, uint16_t by,
               const Surface &to, uint16_t tx, uint16_t ty, uint16_t w, uint16_t h, uint8_t alpha = 255);

    /* The colour table an indexed source (L8, AL44, AL88, L4) looks up: count
     * (1 .. 256) colours 0xAARRGGBB, for the foreground layer - copy()'s
     * source and blend()'s fg - or (background true) blend()'s bg. Kept until
     * set again. */
    bool setPalette(const uint32_t *argb, uint16_t count, bool background = false);

    /* The colour (0xRRGGBB) an alpha-only source (A8, A4) takes, its pixels
     * giving the alpha: for the foreground layer, or (background true)
     * blend()'s bg. Kept until set again. */
    bool setLayerColor(uint32_t rgb, bool background = false);

    /* Bits a pixel of `format`. */
    static uint8_t bitsOf(Format format);

private:
    bool run(uint32_t mode, uint16_t w, uint16_t h);
};
