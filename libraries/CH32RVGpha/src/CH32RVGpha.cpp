/* CH32RVGpha: the GPHA's three modes (CH32H417RM 44.2.11; CTLR.MODE 001
 * memory to memory with pixel format conversion, 010 with blending, 011
 * register to memory). A layer is an address, the pixels to skip at the end
 * of each line (pitch - w) and a format; NLR holds the pixels a line and the
 * lines. The hardware checks the configuration and refuses a bad one (CEIF):
 * that is a false here.
 *
 * Measured on CH32H417 (2026-11-19): OCOLR, the fill's colour, is ARGB8888
 * whatever the output format (the STM32 DMA2D's takes the output's); the
 * conversions to RGB565 keep the high bits as the manual's tables say; a
 * blend comes out up to 3 below the exact (a * f + (255 - a) * b) / 255 per
 * channel (white over black, opaque: 0xFD); a colour table takes the CPU's
 * writes (CWRS / CDAT with the enable bit), not the GPHA's own load. */
#include "CH32RVGpha.h"

#include "internal/ch32rv_registers.h"

#if defined(CH32RV_GPHA_BASE)

#define GPHA(o)       CH32RV_REG32(CH32RV_GPHA_BASE + (o))
#define GPHA_CTLR     GPHA(0x00u)
#define GPHA_ISR      GPHA(0x04u)
#define GPHA_IFCR     GPHA(0x08u)
#define GPHA_FGMAR    GPHA(0x0Cu)
#define GPHA_FGOR     GPHA(0x10u)
#define GPHA_BGMAR    GPHA(0x14u)
#define GPHA_BGOR     GPHA(0x18u)
#define GPHA_FGPFCCR  GPHA(0x1Cu)
#define GPHA_BGPFCCR  GPHA(0x24u)
#define GPHA_OPFCCR   GPHA(0x34u)
#define GPHA_OCOLR    GPHA(0x38u)
#define GPHA_OMAR     GPHA(0x3Cu)
#define GPHA_OOR      GPHA(0x40u)
#define GPHA_NLR      GPHA(0x44u)
#define GPHA_FGCWRS   GPHA(0x50u)
#define GPHA_FGCDAT   GPHA(0x54u)
#define GPHA_BGCWRS   GPHA(0x58u)
#define GPHA_BGCDAT   GPHA(0x5Cu)
#define CWRS_CLUT_EN  (1u << 8)

#define CTLR_START    (1u << 0)
#define MODE(m)       ((uint32_t)(m) << 16)
#define ISR_TCIF      (1u << 1)
#define ISR_CEIF      (1u << 5)
#define PFCCR_AM_MUL  (2u << 16)               /* alpha: the pixel's times ALPHA / 255 */
#define PFCCR_ALPHA(a) ((uint32_t)(a) << 24)

namespace {

uint8_t *at(const CH32RVGpha::Surface &s, uint16_t x, uint16_t y)
{
    const uint32_t bits = CH32RVGpha::bitsOf(s.format);
    return static_cast<uint8_t *>(s.pixels) + ((uint32_t)y * s.pitch + x) * bits / 8u;
}

bool fits(const CH32RVGpha::Surface &s, uint16_t x, uint16_t w)
{
    return s.pixels && w && (uint32_t)x + w <= s.pitch;
}

bool output(CH32RVGpha::Format f)
{
    return f <= CH32RVGpha::ARGB4444;
}

}  // namespace

uint8_t CH32RVGpha::bitsOf(Format format)
{
    switch (format) {
    case ARGB8888:
        return 32;
    case RGB888:
        return 24;
    case RGB565:
    case ARGB1555:
    case ARGB4444:
    case AL88:
        return 16;
    case L8:
    case AL44:
    case A8:
        return 8;
    default:
        return 4;
    }
}

bool CH32RVGpha::run(uint32_t mode, uint16_t w, uint16_t h)
{
    GPHA_NLR = (uint32_t)w << 16 | h;
    GPHA_IFCR = 0x3Eu;
    GPHA_CTLR = MODE(mode) | CTLR_START;
    for (uint32_t t = 0; t < 50000000u; t++) {
        const uint32_t isr = GPHA_ISR;
        if (isr & ISR_CEIF) {
            GPHA_IFCR = ISR_CEIF;
            return false;
        }
        if ((isr & ISR_TCIF) || !(GPHA_CTLR & CTLR_START)) {
            GPHA_IFCR = ISR_TCIF;
            __asm__ volatile("fence rw, rw" ::: "memory");
            return true;
        }
    }
    GPHA_CTLR = MODE(mode) | (1u << 2);       /* ABORT */
    return false;
}

bool CH32RVGpha::setPalette(const uint32_t *argb, uint16_t count, bool background)
{
    if (!argb || count < 1u || count > 256u) {
        return false;
    }
    ch32rv_clock_enable(GPHA);
    /* Written by the CPU, an entry at a time with the table's enable bit set:
     * the GPHA's own load (CMAR, CS, START) left the table as it was on
     * CH32H417 (read back unchanged, an L8 copy wrong; 2026-11-19), the
     * CPU's writes took. The enable stays set. */
    volatile uint32_t &index = background ? GPHA_BGCWRS : GPHA_FGCWRS;
    volatile uint32_t &data = background ? GPHA_BGCDAT : GPHA_FGCDAT;
    for (uint16_t i = 0; i < count; i++) {
        index = CWRS_CLUT_EN | i;
        data = argb[i];
    }
    return true;
}

bool CH32RVGpha::fill(const Surface &to, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t argb)
{
    if (!fits(to, x, w) || !h || !output(to.format)) {
        return false;
    }
    ch32rv_clock_enable(GPHA);
    GPHA_OPFCCR = to.format;
    GPHA_OCOLR = argb;                         /* ARGB8888 whatever the output's format (measured: RGB565 too) */
    GPHA_OMAR = (uint32_t)(uintptr_t)at(to, x, y);
    GPHA_OOR = to.pitch - w;
    return run(3, w, h);
}

bool CH32RVGpha::copy(const Surface &from, uint16_t fx, uint16_t fy, const Surface &to, uint16_t tx, uint16_t ty,
                      uint16_t w, uint16_t h)
{
    if (!fits(from, fx, w) || !fits(to, tx, w) || !h || !output(to.format)) {
        return false;
    }
    ch32rv_clock_enable(GPHA);
    GPHA_FGMAR = (uint32_t)(uintptr_t)at(from, fx, fy);
    GPHA_FGOR = from.pitch - w;
    GPHA_FGPFCCR = from.format;
    GPHA_OPFCCR = to.format;
    GPHA_OMAR = (uint32_t)(uintptr_t)at(to, tx, ty);
    GPHA_OOR = to.pitch - w;
    return run(1, w, h);
}

bool CH32RVGpha::blend(const Surface &fg, uint16_t fx, uint16_t fy, const Surface &bg, uint16_t bx, uint16_t by,
                       const Surface &to, uint16_t tx, uint16_t ty, uint16_t w, uint16_t h, uint8_t alpha)
{
    if (!fits(fg, fx, w) || !fits(bg, bx, w) || !fits(to, tx, w) || !h || !output(to.format)) {
        return false;
    }
    ch32rv_clock_enable(GPHA);
    GPHA_FGMAR = (uint32_t)(uintptr_t)at(fg, fx, fy);
    GPHA_FGOR = fg.pitch - w;
    GPHA_FGPFCCR = fg.format | PFCCR_AM_MUL | PFCCR_ALPHA(alpha);
    GPHA_BGMAR = (uint32_t)(uintptr_t)at(bg, bx, by);
    GPHA_BGOR = bg.pitch - w;
    GPHA_BGPFCCR = bg.format;
    GPHA_OPFCCR = to.format;
    GPHA_OMAR = (uint32_t)(uintptr_t)at(to, tx, ty);
    GPHA_OOR = to.pitch - w;
    return run(2, w, h);
}

#else

uint8_t CH32RVGpha::bitsOf(Format) { return 0; }
bool CH32RVGpha::run(uint32_t, uint16_t, uint16_t) { return false; }
bool CH32RVGpha::fill(const Surface &, uint16_t, uint16_t, uint16_t, uint16_t, uint32_t) { return false; }
bool CH32RVGpha::setPalette(const uint32_t *, uint16_t, bool) { return false; }
bool CH32RVGpha::copy(const Surface &, uint16_t, uint16_t, const Surface &, uint16_t, uint16_t, uint16_t, uint16_t)
{
    return false;
}
bool CH32RVGpha::blend(const Surface &, uint16_t, uint16_t, const Surface &, uint16_t, uint16_t, const Surface &,
                       uint16_t, uint16_t, uint16_t, uint16_t, uint8_t)
{
    return false;
}

#endif
