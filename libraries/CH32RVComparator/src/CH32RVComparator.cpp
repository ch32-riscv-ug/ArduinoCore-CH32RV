#include "CH32RVComparator.h"

#include "internal/ch32rv_gpio.h"
#include "internal/ch32rv_registers.h"

namespace {

enum : uint8_t { ROLE_P = 0, ROLE_N = 1, ROLE_OUT = 2 };
constexpr uint8_t NONE = 0xFF;

#if defined(CH32RV_CMP_PADS)
struct CmpPad {
    uint8_t unit, role, value, af, pin;    /* value: the selection field's; af: the output pad's (per-pin AF) */
};
const CmpPad pads[] = CH32RV_CMP_PADS;
#endif

/* Where one family keeps a comparator's fields (each family's reference
 * manual). Unit n's fields start `at + shift * (n - 1)` bits up; `mask` covers
 * them there, so the bits beside them (filters, wake-up, lock) stay. outNone:
 * MODE's value for no pad (an inner timer channel, or off). opa / ref: the
 * PSEL / NSEL value of the inner input (NONE: none). hyst: the levels in mV,
 * code 1 .. 3 (0: none). */
struct Layout {
    uint32_t reg, key, status;
    uint8_t at, shift, units;
    uint8_t mode, nsel, psel, outNone, opa, ref;
    uint8_t hyst[3];
    uint32_t mask;
    uint8_t enBit, hystBit, hystAt, hystHigh1, hystHigh2, vrefAt;
};

/* Field positions beyond the unit's own (relative to it unless said):
 * enBit; the hysteresis as one bit hystBit, or a 2-bit code at hystAt
 * (absolute), with a second bit above it for unit 1 / 2 at hystHigh1 /
 * hystHigh2 (absolute); the inner reference's level at vrefAt (absolute). */
#if defined(CH32RV_CMP_LAYOUT)
constexpr Layout L = CH32RV_CMP_LAYOUT;      /* the variant's, from the family's reference manual */
#endif

#if defined(CH32RV_CMP_LAYOUT) && defined(CH32RV_CMP_PADS)
#define CH32RV_CMP_READY 1

unsigned base(uint8_t unit)
{
    return L.at + L.shift * (unit - 1u);
}

void unlock()
{
#if defined(CH32RV_CMP_CLKEN_ADDR)
    CH32RV_REG32(CH32RV_CMP_CLKEN_ADDR) |= CH32RV_CMP_CLKEN_MASK;
#endif
    if (L.key) {
        CH32RV_REG32(L.key) = 0x45670123u;             /* KEY1, KEY2: the lock bit clears */
        CH32RV_REG32(L.key) = 0xCDEF89ABu;
    }
}

/* Hysteresis code 0 .. 3 as bits of the register (with the mask they replace). */
void hysteresis(uint8_t unit, uint8_t code, uint32_t &bits, uint32_t &mask)
{
    bits = mask = 0;
    if (L.hystAt != NONE) {
        mask = 3u << L.hystAt;
        bits = (uint32_t)code << L.hystAt;
        return;
    }
    if (L.hystBit == NONE) {
        return;
    }
    mask = 1u << (base(unit) + L.hystBit);
    bits = (code & 1u) ? mask : 0;
    const uint8_t high = unit == 1u ? L.hystHigh1 : unit == 2u ? L.hystHigh2 : NONE;
    if (high != NONE) {
        mask |= 1u << high;
        bits |= (code & 2u) ? 1u << high : 0;
    }
}
#endif

}  // namespace

bool CH32RVComparator::select(int pin, uint8_t role, uint8_t &sel, uint8_t &af) const
{
#if defined(CH32RV_CMP_READY)
    if (_unit < 1u || _unit > L.units) {
        return false;
    }
    if (pin == OPA_OUTPUT && role == ROLE_P && L.opa != NONE) {
        sel = L.opa;
        return true;
    }
    if (pin == REFERENCE && role == ROLE_N && L.ref != NONE) {
        sel = L.ref;
        return true;
    }
    if (pin < 0) {
        return false;
    }
    const uint8_t want = (uint8_t)CH32RV_PIN_RESOLVE(pin);
    for (const CmpPad &p : pads) {
        if (p.unit == _unit && p.role == role && p.pin == want) {
            sel = p.value;
            af = p.af;
            return true;
        }
    }
#else
    (void)pin;
    (void)role;
    (void)sel;
    (void)af;
#endif
    return false;
}

/* Writes the unit's fields with the hysteresis and the reference. */
void CH32RVComparator::apply()
{
#if defined(CH32RV_CMP_READY)
    unlock();
    uint32_t hb, hm;
    hysteresis(_unit, _hyst, hb, hm);
    uint32_t mask = (L.mask << base(_unit)) | hm, bits = (_fields << base(_unit)) | hb;
    if (L.vrefAt != NONE) {
        mask |= 3u << L.vrefAt;
        if (((_fields >> L.nsel) & 3u) == L.ref) {     /* the bias only while - takes it */
            bits |= (uint32_t)_ref << L.vrefAt;
        }
    }
    volatile uint32_t &r = CH32RV_REG32(L.reg);
    r = (r & ~mask) | bits;
#endif
}

bool CH32RVComparator::begin(int positivePin, int negativePin, int outPin)
{
#if defined(CH32RV_CMP_READY)
    uint8_t p, n, o = L.outNone, af = 0;
    if (!select(positivePin, ROLE_P, p, af) || !select(negativePin, ROLE_N, n, af) ||
        (outPin >= 0 && !select(outPin, ROLE_OUT, o, af))) {
        return false;
    }
    _out = -1;
    if (outPin >= 0) {
        const uint8_t port = (uint8_t)CH32RV_PIN_PORT(outPin), bit = (uint8_t)CH32RV_PIN_BIT(outPin);
        ch32rv_gpio_clock_enable(port);
#if CH32RV_GPIO_PER_PIN_AF
        ch32rv_pin_set_af((uint8_t)CH32RV_PIN_RESOLVE(outPin), af);
#endif
        ch32rv_gpio_set_config(port, bit, CH32RV_GPIO_CFG_AF_PP_50M);
        _out = (int8_t)outPin;
    }
    _fields = 1u << L.enBit | (uint32_t)o << L.mode | (uint32_t)n << L.nsel | (uint32_t)p << L.psel;
    apply();
    _begun = true;
    return true;
#else
    (void)positivePin;
    (void)negativePin;
    (void)outPin;
    return false;
#endif
}

int CH32RVComparator::read() const
{
    if (!_begun) {
        return -1;
    }
#if defined(CH32RV_CMP_READY)
    if (L.status) {
        return (int)(CH32RV_REG32(L.status) & 1u);
    }
#endif
    return _out >= 0 ? digitalRead(_out) : -1;
}

bool CH32RVComparator::setHysteresis(uint8_t millivolts)
{
#if defined(CH32RV_CMP_READY)
    if (!L.hyst[0]) {
        return false;
    }
    uint8_t code = 0;
    unsigned best = millivolts;                        /* off: |mV - 0| */
    for (uint8_t k = 0; k < 3 && L.hyst[k]; k++) {
        const unsigned d = millivolts > L.hyst[k] ? millivolts - L.hyst[k] : L.hyst[k] - millivolts;
        if (d < best) {
            best = d;
            code = (uint8_t)(k + 1u);
        }
    }
    _hyst = code;
    if (_begun) {
        apply();
    }
    return true;
#else
    (void)millivolts;
    return false;
#endif
}

bool CH32RVComparator::setReference(uint8_t quarters)
{
#if defined(CH32RV_CMP_READY)
    if (L.vrefAt == NONE || quarters < 1u || quarters > 3u) {
        return false;
    }
    _ref = quarters;
    if (_begun) {
        apply();
    }
    return true;
#else
    (void)quarters;
    return false;
#endif
}

void CH32RVComparator::end()
{
    if (!_begun) {
        return;
    }
#if defined(CH32RV_CMP_READY)
    _fields &= ~(1u << L.enBit);
    apply();
#endif
    if (_out >= 0) {
        pinMode(_out, INPUT);
    }
    _begun = false;
}
