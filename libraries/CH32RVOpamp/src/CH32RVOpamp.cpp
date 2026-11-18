#include "CH32RVOpamp.h"

#include "internal/ch32rv_gpio.h"
#include "internal/ch32rv_registers.h"

namespace {

#if defined(CH32RV_OPA_PADS)
struct OpaPad {
    uint8_t unit, role, value, pin;    /* value: the selection field's, from the reference manual */
};
const OpaPad pads[] = CH32RV_OPA_PADS;
#endif

enum : uint8_t { ROLE_P = 0, ROLE_N = 1, ROLE_OUT = 2 };
constexpr uint8_t NONE = 0xFF;

/* Where one family keeps an op-amp's fields (each family's reference manual).
 * The unit's control register is `reg + step * (unit - 1)`, its fields `at +
 * shift * (unit - 1)` bits up from there; `mask` covers them (relative to
 * that), so the bits beside them (trims, polling, lock) stay. PGA: FB_EN set,
 * the gain's code into the field at `gain`, `gainCode` for the lowest gain,
 * doubling from minGain[unit - 1] (0: none). */
struct Layout {
    uint32_t reg, step, key;
    uint8_t at, shift, units;
    uint8_t mode, psel, nsel, fb, gain, gainCode;
    uint8_t minGain[3];
    uint32_t mask;
};

#if defined(CH32RV_SERIES_CH32V003)
/* EXTEN_CTR: OPA_EN 16, OPA_NSEL 17, OPA_PSEL 18 */
constexpr Layout L = {0x40023800u, 0, 0, 16, 0, 1, NONE, 2, 1, NONE, NONE, 0, {0, 0, 0}, 0x7u};
#elif defined(CH32RV_SERIES_CH32V203) || defined(CH32RV_SERIES_CH32V208) || defined(CH32RV_SERIES_CH32V303) || \
    defined(CH32RV_SERIES_CH32V305) || defined(CH32RV_SERIES_CH32V307) || defined(CH32RV_SERIES_CH32V317)
/* OPA_CTLR: a nibble each, EN 0, MODE 1, NSEL 2, PSEL 3 */
constexpr Layout L = {0x40023804u, 0, 0, 0, 4, 4, 1, 3, 2, NONE, NONE, 0, {0, 0, 0}, 0xFu};
#elif defined(CH32RV_SERIES_CH32V407) || defined(CH32RV_SERIES_CH32V467)
/* OPA_CTLR: EN 0, MODE 1, NSEL 4:2 (PGA x4 .. x32: 2 .. 5), PSEL 5, PGADIF 6, FB_EN 7, HS 8 */
constexpr Layout L = {0x40023804u, 0, 0, 0, 0, 1, 1, 5, 2, 7, 2, 2, {4, 0, 0}, 0x1FFu};
#elif defined(CH32RV_SERIES_CH32V005) || defined(CH32RV_SERIES_CH32V006) || defined(CH32RV_SERIES_CH32V007) || \
    defined(CH32RV_SERIES_CH32M007)
/* OPA_CTLR1: EN 0, MODE 2:1, PSEL 5:4, NSEL 10:8 (PGA x4 .. x32: 3 .. 6), FB_EN 11, PGADIF 12; OPA_KEY */
constexpr Layout L = {0x40024004u, 0, 0x40024010u, 0, 0, 1, 1, 4, 8, 11, 8, 3, {4, 0, 0}, 0x1F37u};
#elif defined(CH32RV_SERIES_CH32V205)
/* OPA_CTLR1 / 2: EN 0, MODE 2:1, PSEL 4:3 (OPA2: 3), NSEL 5, PGA_SEL 9:8 (OPA1 x8 .. x64, OPA2 x4 .. x32),
 * FB_EN 10, PGADIF 11, HS 12; OPA_KEY */
constexpr Layout L = {0x40026004u, 4, 0x40026014u, 0, 0, 2, 1, 3, 5, 10, 8, 0, {8, 4, 0}, 0x1FFFu};
#elif defined(CH32RV_SERIES_CH32L103) || defined(CH32RV_SERIES_CH32M103)
/* OPA_CTLR1: EN 0, MODE 3:1, PSEL 6:4, FB_EN 7, NSEL 11:8 (PGA x8 .. x64: 7 .. 10), LP 12; OPCM_KEY */
constexpr Layout L = {0x40026004u, 0, 0x40026014u, 0, 0, 1, 1, 4, 8, 7, 8, 7, {8, 0, 0}, 0x1FFFu};
#elif defined(CH32RV_SERIES_CH32X035) || defined(CH32RV_SERIES_CH32X033)
/* OPA_CTLR1: OPA1 at 0, OPA2 at 16: EN 0, MODE 1, PSEL 4:3, FB_EN 5, NSEL 8:6 (PGA x4 .. x32: 3 .. 6); OPA_KEY */
constexpr Layout L = {0x40026004u, 0, 0x4002600Cu, 0, 16, 2, 1, 3, 6, 5, 6, 3, {4, 4, 0}, 0x1FFu};
#elif defined(CH32RV_SERIES_CH32H415) || defined(CH32RV_SERIES_CH32H416) || defined(CH32RV_SERIES_CH32H417)
/* OPA_CTLR1 / 2 / 3: EN 0, MODE 2:1, PSEL 3, NSEL 6:4 (PGA x8 .. x64: 2 .. 5), FB_EN 8, PGADIF 9, HS 10 */
constexpr Layout L = {0x40017800u, 4, 0, 0, 0, 3, 1, 3, 4, 8, 4, 2, {8, 8, 8}, 0x7FFu};
#else
#define CH32RV_OPA_NO_LAYOUT 1
#endif

#if !defined(CH32RV_OPA_NO_LAYOUT) && defined(CH32RV_OPA_PADS)
#define CH32RV_OPA_READY 1

volatile uint32_t &control(uint8_t unit)
{
    return CH32RV_REG32(L.reg + L.step * (unit - 1u));
}

unsigned base(uint8_t unit)
{
    return L.at + L.shift * (unit - 1u);
}

/* Sets one unit's fields: the rest of the register is kept. */
void apply(uint8_t unit, uint32_t fields)
{
#if defined(CH32RV_OPA_CLKEN_ADDR)
    CH32RV_REG32(CH32RV_OPA_CLKEN_ADDR) |= CH32RV_OPA_CLKEN_MASK;
#endif
    if (L.key) {
        CH32RV_REG32(L.key) = 0x45670123u;             /* KEY1, KEY2: the lock bit clears */
        CH32RV_REG32(L.key) = 0xCDEF89ABu;
    }
    volatile uint32_t &r = control(unit);
    const unsigned at = base(unit);
    r = (r & ~(L.mask << at)) | fields << at;
}
#endif

}  // namespace

/* The selection value of `pin` as this op-amp's `role`, or false. */
bool CH32RVOpamp::select(int pin, uint8_t role, uint8_t &sel) const
{
#if defined(CH32RV_OPA_READY)
    if (pin < 0 || _unit < 1u || _unit > L.units) {
        return false;
    }
    const uint8_t want = (uint8_t)CH32RV_PIN_RESOLVE(pin);
    for (const OpaPad &p : pads) {
        if (p.unit == _unit && p.role == role && p.pin == want) {
            sel = p.value;
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

#if defined(CH32RV_OPA_READY)
/* The output pad whose selection value is 0. */
static bool firstOut(uint8_t unit, int &pin)
{
    for (const OpaPad &p : pads) {
        if (p.unit == unit && p.role == ROLE_OUT && p.value == 0) {
            pin = p.pin;
            return true;
        }
    }
    return false;
}

static void analogPad(int pin)
{
    const uint8_t port = (uint8_t)CH32RV_PIN_PORT(pin), bit = (uint8_t)CH32RV_PIN_BIT(pin);
    ch32rv_gpio_clock_enable(port);
    ch32rv_gpio_set_config(port, bit, CH32RV_GPIO_CFG_IN_ANALOG);
}
#endif

bool CH32RVOpamp::begin(int positivePin, int negativePin, int outPin)
{
#if defined(CH32RV_OPA_READY)
    uint8_t p, n, o = 0;
    if (outPin < 0 && !firstOut(_unit, outPin)) {
        return false;
    }
    if (!select(positivePin, ROLE_P, p) || !select(negativePin, ROLE_N, n) || !select(outPin, ROLE_OUT, o)) {
        return false;
    }
    analogPad(outPin);
    uint32_t f = 1u | (uint32_t)p << L.psel | (uint32_t)n << L.nsel;
    if (L.mode != NONE) {
        f |= (uint32_t)o << L.mode;
    }
    apply(_unit, f);
    _begun = true;
    return true;
#else
    (void)positivePin;
    (void)negativePin;
    (void)outPin;
    return false;
#endif
}

bool CH32RVOpamp::beginPga(int positivePin, uint8_t gain, int outPin)
{
#if defined(CH32RV_OPA_READY)
    if (L.fb == NONE || _unit < 1u || _unit > L.units || !L.minGain[_unit - 1u]) {
        return false;
    }
    uint8_t step = 0;
    for (unsigned g = L.minGain[_unit - 1u]; g != gain; g <<= 1) {
        if (++step > 3u) {
            return false;                              /* four gains, doubling */
        }
    }
    uint8_t p, o = 0;
    if (outPin < 0 && !firstOut(_unit, outPin)) {
        return false;
    }
    if (!select(positivePin, ROLE_P, p) || !select(outPin, ROLE_OUT, o)) {
        return false;
    }
    analogPad(outPin);
    /* FB_EN closes the internal divider; PGADIF 0 takes its foot to GND */
    uint32_t f = 1u | (uint32_t)p << L.psel | 1u << L.fb | (uint32_t)(L.gainCode + step) << L.gain;
    if (L.mode != NONE) {
        f |= (uint32_t)o << L.mode;
    }
    apply(_unit, f);
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
#if defined(CH32RV_OPA_READY)
    apply(_unit, (control(_unit) >> base(_unit)) & L.mask & ~1u);
#endif
    _begun = false;
}
