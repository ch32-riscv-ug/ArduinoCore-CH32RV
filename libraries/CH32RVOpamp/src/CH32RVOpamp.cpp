#include "CH32RVOpamp.h"

#include "internal/ch32rv_gpio.h"
#include "internal/ch32rv_registers.h"

namespace {

#if defined(CH32RV_OPA_PADS)
struct OpaPad {
    uint8_t unit, role, index, pin;
};
const OpaPad pads[] = CH32RV_OPA_PADS;
#endif

enum : uint8_t { ROLE_P = 0, ROLE_N = 1, ROLE_OUT = 2 };

/* The selection registers (each family's reference manual / EVT header). */
#if defined(CH32RV_SERIES_CH32V003)
#define OPA_STYLE_V003 1
#define OPA_REG CH32RV_REG32(0x40023800u)              /* EXTEN_CTR: EN 16, NSEL 17, PSEL 18 */
#elif defined(CH32RV_SERIES_CH32V203) || defined(CH32RV_SERIES_CH32V208) || defined(CH32RV_SERIES_CH32V303) || \
    defined(CH32RV_SERIES_CH32V305) || defined(CH32RV_SERIES_CH32V307) || defined(CH32RV_SERIES_CH32V317)
#define OPA_STYLE_V20X 1
#define OPA_REG CH32RV_REG32(0x40023804u)              /* OPA_CR: a nibble each, EN 0, MODE 1, NSEL 2, PSEL 3 */
#elif defined(CH32RV_SERIES_CH32V407) || defined(CH32RV_SERIES_CH32V467)
#define OPA_STYLE_V407 1
#define OPA_REG CH32RV_REG32(0x40023804u)              /* OPA_CR: EN 0, MODE 1, NSEL 4:2, PSEL 5, FB_EN 7 */
#endif

}  // namespace

/* The selection value of `pin` as this op-amp's `role`: its index above the
 * role's lowest (V4x7 numbers its pads from 1, the field from 0). */
bool CH32RVOpamp::select(int pin, uint8_t role, uint8_t &sel) const
{
#if defined(CH32RV_OPA_PADS)
    if (pin < 0) {
        return false;
    }
    const uint8_t want = (uint8_t)CH32RV_PIN_RESOLVE(pin);
    uint8_t lowest = 0xFF;
    for (const OpaPad &p : pads) {
        if (p.unit == _unit && p.role == role && p.index < lowest) {
            lowest = p.index;
        }
    }
    for (const OpaPad &p : pads) {
        if (p.unit == _unit && p.role == role && p.pin == want) {
            sel = (uint8_t)(p.index - lowest);
            return true;
        }
    }
#else
    (void)pin;
    (void)role;
    (void)sel;
#endif
    return false;
}

static bool firstOut(uint8_t unit, int &pin)
{
#if defined(CH32RV_OPA_PADS)
    uint8_t best = 0xFF;
    for (const OpaPad &p : pads) {
        if (p.unit == unit && p.role == ROLE_OUT && p.index < best) {
            best = p.index;
            pin = p.pin;
        }
    }
    return best != 0xFF;
#else
    (void)unit;
    (void)pin;
    return false;
#endif
}

static void analogPad(int pin)
{
    const uint8_t port = (uint8_t)CH32RV_PIN_PORT(pin), bit = (uint8_t)CH32RV_PIN_BIT(pin);
    ch32rv_gpio_clock_enable(port);
    ch32rv_gpio_set_config(port, bit, CH32RV_GPIO_CFG_IN_ANALOG);
}

bool CH32RVOpamp::begin(int positivePin, int negativePin, int outPin)
{
    uint8_t p, n, o = 0;
    if (outPin < 0 && !firstOut(_unit, outPin)) {
        return false;
    }
    if (!select(positivePin, ROLE_P, p) || !select(negativePin, ROLE_N, n) || !select(outPin, ROLE_OUT, o)) {
        return false;
    }
#if defined(OPA_STYLE_V003)
    if (_unit != 1 || p > 1u || n > 1u) {
        return false;
    }
    analogPad(outPin);
    OPA_REG = (OPA_REG & ~(7u << 16)) | (1u << 16) | ((uint32_t)n << 17) | ((uint32_t)p << 18);
#elif defined(OPA_STYLE_V20X)
    if (_unit < 1u || _unit > 4u || p > 1u || n > 1u || o > 1u) {
        return false;
    }
    analogPad(outPin);
    const unsigned shift = 4u * (_unit - 1u);
    OPA_REG = (OPA_REG & ~(0xFu << shift)) |
              ((1u | (uint32_t)o << 1 | (uint32_t)n << 2 | (uint32_t)p << 3) << shift);
#elif defined(OPA_STYLE_V407)
    if (_unit != 1u || p > 1u || n > 1u || o > 1u) {
        return false;
    }
    o ^= 1u;                                           /* MODE1 0 is PB1 (OUT2 in the pin tables), 1 PA7 (OUT1) */
    analogPad(outPin);
    OPA_REG = (OPA_REG & ~0x1FFu) | 1u | (uint32_t)o << 1 | (uint32_t)n << 2 | (uint32_t)p << 5;
#else
    (void)p;
    (void)n;
    (void)o;
    return false;
#endif
    _begun = true;
    return true;
}

bool CH32RVOpamp::beginPga(int positivePin, uint8_t gain, int outPin)
{
#if defined(OPA_STYLE_V407)
    uint8_t p, o = 0;
    uint32_t nsel;
    switch (gain) {
    case 4: nsel = 2; break;
    case 8: nsel = 3; break;
    case 16: nsel = 4; break;
    case 32: nsel = 5; break;
    default: return false;
    }
    if (outPin < 0 && !firstOut(_unit, outPin)) {
        return false;
    }
    if (_unit != 1u || !select(positivePin, ROLE_P, p) || !select(outPin, ROLE_OUT, o) || p > 1u || o > 1u) {
        return false;
    }
    o ^= 1u;                                           /* MODE1 0 is PB1, 1 PA7: as begin() */
    analogPad(outPin);
    /* FB_EN closes the gain's divider; PGADIF 0 takes its foot to GND */
    OPA_REG = (OPA_REG & ~0x1FFu) | 1u | (uint32_t)o << 1 | nsel << 2 | (uint32_t)p << 5 | 1u << 7;
    _begun = true;
    return true;
#else
    (void)positivePin;
    (void)gain;
    (void)outPin;
    return false;
#endif
}

void CH32RVOpamp::end()
{
    if (!_begun) {
        return;
    }
#if defined(OPA_STYLE_V003)
    OPA_REG &= ~(1u << 16);
#elif defined(OPA_STYLE_V20X)
    OPA_REG &= ~(1u << (4u * (_unit - 1u)));
#elif defined(OPA_STYLE_V407)
    OPA_REG &= ~1u;
#endif
    _begun = false;
}
