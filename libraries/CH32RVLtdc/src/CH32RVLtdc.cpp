/* CH32RVLtdc.h. The registers are the STM32 LTDC's packed without its gaps
 * (EVT ch32h417.h: SSCR at 0x00 .. CDSR at 0x30, layer 1 at 0x34 and layer 2
 * at 0x64, twelve words each - CR, WHPCR, WVPCR, CKCR, PFCR, CACR, DCCR,
 * BFCR, CFBAR, CFBLR, CFBLNR, CLUTWR), and the layer window counted from the
 * active area's corner (the EVT's LTDC_Display). The pixel clock: RCC_CFGR2
 * LTDCSRC [15:14] (0 the system PLL) and LTDCDIV [13:8] (divide by n + 1). */
#include "CH32RVLtdc.h"

#if CH32RV_HAS_LTDC

#include <stdlib.h>

#include "internal/ch32rv_gpio.h"
#include "internal/ch32rv_registers.h"

#define BASE 0x40014800u
#define R(off) CH32RV_REG32(BASE + (off))
#define SSCR  R(0x00)
#define BPCR  R(0x04)
#define AWCR  R(0x08)
#define TWCR  R(0x0C)
#define GCR   R(0x10)
#define SRCR  R(0x14)
#define BCCR  R(0x18)
#define CPSR  R(0x2C)
#define L(n, off) CH32RV_REG32(BASE + ((n) == 1 ? 0x34u : 0x64u) + (off))

#define GCR_LTDCEN (1u << 0)
#define GCR_PCPOL  (1u << 28)
#define GCR_DEPOL  (1u << 29)
#define GCR_VSPOL  (1u << 30)
#define GCR_HSPOL  (1u << 31)
#define SRCR_IMR   (1u << 0)
#define SRCR_VBR   (1u << 1)

#define RCC_HB2PRSTR CH32RV_REG32(0x40021010u)
#define RCC_HB2PCENR CH32RV_REG32(0x4002101Cu)
#define RCC_CFGR2    CH32RV_REG32(0x40021030u)
#define LTDC_RCC_BIT (1u << 30)

namespace {
struct Pad {
    uint8_t role;   /* 0-7 R0-R7, 8-15 G0-G7, 16-23 B0-B7, 24 CLK, 25 HSYNC, 26 VSYNC, 27 DE */
    uint8_t pad;
    uint32_t value, value2;
};
const Pad pads[] = CH32RV_LTDC_PADS;
const uint8_t bytes_per_pixel[] = {4, 3, 2, 2, 2, 1, 1, 2};
/* With no layer on, the LTDC sent no colour at all - not even the background
 * - and with one on, the background showed only within its window (H417,
 * 2026-11-23: the data lines low; a transparent layer over the whole area
 * and the background showed). So while the sketch has no layer on, layer 1
 * is a transparent one over the whole area: one RGB565 line of zeros
 * (constant alpha 0), its pitch 0 so that every line reads it again. */
uint8_t *clear_line;

void clear_layer(bool on, uint16_t w = 0, uint16_t h = 0)
{
    if (!on || !clear_line) {
        L(1, 0x00) = 0;
        return;
    }
    L(1, 0x04) = (uint32_t)w << 16;              /* x 0 .. w */
    L(1, 0x08) = (uint32_t)h << 16;              /* y 0 .. h */
    L(1, 0x10) = 2;                              /* RGB565 */
    L(1, 0x14) = 0;                              /* constant alpha 0 */
    L(1, 0x18) = 0;
    L(1, 0x1C) = 0x600u | 0x7u;
    L(1, 0x20) = (uint32_t)(uintptr_t)clear_line;
    L(1, 0x24) = (uint32_t)(2u * w) + 31u;       /* pitch 0: the same line again */
    L(1, 0x28) = h;
    L(1, 0x00) = 1;
}
}   // namespace

bool CH32RVLtdc::pad(int pin, uint8_t role)
{
    if (pin < 0) {
        return true;
    }
    for (const Pad &p : pads) {
        if (p.role == role && p.pad == (uint8_t)pin) {
            ch32rv_pin_set_af(p.pad, p.value);
            const uint8_t port = (uint8_t)CH32RV_PIN_PORT(pin);
            ch32rv_gpio_clock_enable(port);
            ch32rv_gpio_set_config(port, (uint8_t)CH32RV_PIN_BIT(pin), CH32RV_GPIO_CFG_AF_PP_50M);
            return true;
        }
    }
    return false;
}

static bool known(int pin, uint8_t role)
{
    if (pin < 0) {
        return true;
    }
    for (const Pad &p : pads) {
        if (p.role == role && p.pad == (uint8_t)pin) {
            return true;
        }
    }
    return false;
}

bool CH32RVLtdc::setSignalPins(int clk, int hsync, int vsync, int de)
{
    const int s[4] = {clk, hsync, vsync, de};
    for (uint8_t k = 0; k < 4; k++) {
        if (s[k] < 0 || !known(s[k], (uint8_t)(24 + k))) {
            return false;
        }
    }
    for (uint8_t k = 0; k < 4; k++) {
        _signal[k] = s[k];
    }
    return true;
}

bool CH32RVLtdc::setColorPins(const int r[8], const int g[8], const int b[8])
{
    const int *c[3] = {r, g, b};
    for (uint8_t k = 0; k < 24; k++) {
        if (!known(c[k / 8][k % 8], k)) {
            return false;
        }
    }
    for (uint8_t k = 0; k < 24; k++) {
        _color[k] = c[k / 8][k % 8];
    }
    return true;
}

bool CH32RVLtdc::begin(const CH32RVLtdcTiming &t)
{
    end();
    if (_signal[0] < 0 || !t.width || !t.height || !t.hsync || !t.vsync || !t.pixelHz) {
        return false;
    }
    uint32_t div = (CH32RV_CLOCK_SYSCLK_HZ + t.pixelHz - 1u) / t.pixelHz;
    div = div < 1u ? 1u : div > 64u ? 64u : div;
    _pixel_hz = CH32RV_CLOCK_SYSCLK_HZ / div;
    for (uint8_t k = 0; k < 4; k++) {
        pad(_signal[k], (uint8_t)(24 + k));
    }
    for (uint8_t k = 0; k < 24; k++) {
        pad(_color[k], k);
    }
    RCC_CFGR2 = (RCC_CFGR2 & ~(0x3u << 14 | 0x3Fu << 8)) | ((div - 1u) << 8);   /* the system PLL / div */
    RCC_HB2PCENR |= LTDC_RCC_BIT;
    RCC_HB2PRSTR |= LTDC_RCC_BIT;
    RCC_HB2PRSTR &= ~LTDC_RCC_BIT;

    /* The first write right after the reset was lost (SSCR stayed 0: one-pixel HSYNC and one-line
     * VSYNC, H417 2026-11-23): a read first, and SSCR checked. */
    (void)GCR;
    const uint32_t hs = t.hsync - 1u, vs = t.vsync - 1u;
    const uint32_t ahbp = t.hsync + t.hbp - 1u, avbp = t.vsync + t.vbp - 1u;
    const uint32_t aaw = ahbp + t.width, aah = avbp + t.height;
    const uint32_t tw = aaw + t.hfp, th = aah + t.vfp;
    for (uint8_t i = 0; i < 4 && SSCR != (hs << 16 | vs); i++) {
        SSCR = hs << 16 | vs;
    }
    BPCR = ahbp << 16 | avbp;
    AWCR = aaw << 16 | aah;
    TWCR = tw << 16 | th;
    GCR = (t.hsyncHigh ? GCR_HSPOL : 0u) | (t.vsyncHigh ? GCR_VSPOL : 0u) | (t.deHigh ? GCR_DEPOL : 0u) |
          (t.clockInverted ? GCR_PCPOL : 0u);
    BCCR = 0;
    L(2, 0x00) = 0;
    free(clear_line);
    clear_line = static_cast<uint8_t *>(calloc(t.width, 2));
    _w = t.width;
    _h = t.height;
    clear_layer(true, _w, _h);
    _layers = 0;
    SRCR = SRCR_IMR;
    GCR |= GCR_LTDCEN;
    _vstart = (uint16_t)(avbp + 1u);
    _begun = true;
    return true;
}

void CH32RVLtdc::end()
{
    if (!_begun) {
        return;
    }
    GCR &= ~GCR_LTDCEN;
    RCC_HB2PCENR &= ~LTDC_RCC_BIT;
    free(clear_line);
    clear_line = nullptr;
    for (uint8_t k = 0; k < 4; k++) {
        if (_signal[k] >= 0) {
            pinMode(_signal[k], INPUT);
        }
    }
    for (uint8_t k = 0; k < 24; k++) {
        if (_color[k] >= 0) {
            pinMode(_color[k], INPUT);
        }
    }
    _begun = false;
    _pixel_hz = 0;
}

bool CH32RVLtdc::layer(uint8_t n, const void *fb, CH32RVLtdcFormat format, uint16_t x, uint16_t y, uint16_t w,
                       uint16_t h, uint8_t alpha)
{
    if (!_begun || (n != 1 && n != 2) || (unsigned)format > CH32RV_LTDC_AL88) {
        return false;
    }
    if (!fb || !w || !h) {
        L(n, 0x00) = 0;
        _layers &= (uint8_t)~(1u << n);
        if (!_layers) {
            clear_layer(true, _w, _h);
        }
        SRCR = SRCR_VBR;
        return true;
    }
    if (!_layers) {
        clear_layer(false);   /* the sketch's layer shows the background from here */
    }
    _layers |= (uint8_t)(1u << n);
    const uint32_t bpp = bytes_per_pixel[format];
    L(n, 0x04) = (uint32_t)(x + w) << 16 | x;              /* WHPCR: stop, start */
    L(n, 0x08) = (uint32_t)(y + h) << 16 | y;              /* WVPCR */
    L(n, 0x10) = format;                                    /* PFCR */
    L(n, 0x14) = alpha;                                     /* CACR */
    L(n, 0x18) = 0;                                         /* DCCR */
    L(n, 0x1C) = 0x600u | 0x7u;                             /* BFCR: pixel alpha x constant alpha */
    L(n, 0x20) = (uint32_t)(uintptr_t)fb;                   /* CFBAR */
    L(n, 0x24) = (w * bpp) << 16 | (w * bpp + 31u);        /* CFBLR: pitch, line length (+31, the EVT's) */
    L(n, 0x28) = h;                                         /* CFBLNR */
    L(n, 0x00) = 1;                                         /* CR: LEN */
    SRCR = SRCR_VBR;
    return true;
}

void CH32RVLtdc::setBackground(uint32_t rgb)
{
    BCCR = rgb & 0xFFFFFFu;
}

int CH32RVLtdc::line() const
{
    return _begun ? (int)(CPSR & 0xFFFFu) - (int)_vstart : 0;
}

#endif   // CH32RV_HAS_LTDC
