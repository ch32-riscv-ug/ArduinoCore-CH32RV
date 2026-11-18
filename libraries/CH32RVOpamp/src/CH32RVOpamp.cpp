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

#if defined(CH32RV_OPA_LAYOUT)
constexpr Layout L = CH32RV_OPA_LAYOUT;      /* the variant's, from the family's reference manual */
#endif

#if defined(CH32RV_OPA_LAYOUT) && defined(CH32RV_OPA_PADS)
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
