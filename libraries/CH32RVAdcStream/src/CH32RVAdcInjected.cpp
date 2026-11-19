/* CH32RVAdcInjected: ADC1's injected group (CH32RVAdcInjected.h). */
#include "CH32RVAdcInjected.h"

#include "internal/ch32rv_adc_run.h"
#include "internal/ch32rv_irq.h"
#include "internal/ch32rv_registers.h"

#if defined(CH32RV_ADC_JEXTSEL_SHIFT) && defined(CH32RV_ADC_IRQ_HANDLER)
#define INJECTED_READY 1
#endif

namespace {

#define ADC_JSQR  CH32RV_REG32(CH32RV_ADC1_BASE + 0x38u)
#define ADC_JDR(i) CH32RV_REG32(CH32RV_ADC1_BASE + 0x3Cu + 4u * (i))
#if defined(CH32RV_ADC_DUAL_INJECTED_SIMULTANEOUS)
#define ADC2_JSQR  CH32RV_REG32(CH32RV_ADC2_BASE + 0x38u)
#define ADC2_JDR(i) CH32RV_REG32(CH32RV_ADC2_BASE + 0x3Cu + 4u * (i))
#endif
constexpr uint32_t STATR_JEOC = 1u << 2;
constexpr uint32_t CTLR1_JEOCIE = 1u << 7, CTLR1_SCAN = 1u << 8;
constexpr uint32_t CTLR2_JEXTTRIG = 1u << 15, CTLR2_JSWSTART = 1u << 21;

CH32RVAdcInjected *active;

#if defined(INJECTED_READY)
void source(uint32_t value)
{
    const uint32_t mask = ((1u << CH32RV_ADC_JEXTSEL_BITS) - 1u) << CH32RV_ADC_JEXTSEL_SHIFT;
    CH32RV_ADC_CTLR2 = (CH32RV_ADC_CTLR2 & ~mask) | value << CH32RV_ADC_JEXTSEL_SHIFT | CTLR2_JEXTTRIG;
}
#endif

}  // namespace

extern "C" void ch32rv_adc_injected_irq(void)
{
    if (active) {
        active->service();
    }
}

void CH32RVAdcInjected::service()
{
    if (!(CH32RV_ADC_STATR & STATR_JEOC)) {
        return;
    }
    uint16_t v[8];
    for (uint8_t i = 0; i < _count; i++) {
        v[i] = (uint16_t)ADC_JDR(i);
        _values[i] = v[i];
    }
#if defined(CH32RV_ADC_DUAL_INJECTED_SIMULTANEOUS)
    if (_dual) {
        /* ADC2 converted at the same moment and ends with ADC1 (the groups are the same length) */
        for (uint8_t i = 0; i < _count; i++) {
            v[_count + i] = (uint16_t)ADC2_JDR(i);
            _values[_count + i] = v[_count + i];
        }
        CH32RV_ADC_STATR_AT(CH32RV_ADC2_BASE) = ~STATR_JEOC;
    }
#endif
    CH32RV_ADC_STATR = ~STATR_JEOC;
    _rounds = _rounds + 1u;
    if (_callback) {
        _callback(v, (uint8_t)(_dual ? 2u * _count : _count));
    }
}

bool CH32RVAdcInjected::begin(const pin_size_t *pins, uint8_t count)
{
#if defined(INJECTED_READY)
    end();
    if (!pins || count == 0 || count > 4 || active) {
        return false;
    }
    for (uint8_t i = 0; i < count; i++) {
        const pin_size_t pad = CH32RV_PIN_RESOLVE(pins[i]);
        const unsigned channel = CH32RV_PIN_TO_ADC_CHANNEL(pad);
        if (channel == NOT_AN_ANALOG_PIN) {
            return false;
        }
#ifdef CH32RV_PIN_TO_ADC_INSTANCE
        if (CH32RV_PIN_TO_ADC_INSTANCE(pad) != 1) {
            return false;
        }
#endif
        _channels[i] = (uint8_t)channel;
        /* analogRead() starts and calibrates the ADC, puts the pad in analog mode and sets the sample times. */
        (void)analogRead(pins[i]);
    }
    _count = count;
    /* JL = count - 1; the round is the last `count` of JSQ1..JSQ4, its results JDR1.. in order. */
    uint32_t jsqr = (uint32_t)(count - 1u) << 20;
    for (uint8_t i = 0; i < count; i++) {
        jsqr |= (uint32_t)_channels[i] << (5u * (4u - count + i));
    }
    ADC_JSQR = jsqr;
    CH32RV_ADC_CTLR1 |= CTLR1_SCAN | CTLR1_JEOCIE;   /* SCAN: every channel of the round */
    CH32RV_ADC_STATR = ~STATR_JEOC;
    source(CH32RV_ADC_JSWSTART_VALUE);
    active = this;
    _rounds = 0;
    ch32rv_adc_irq_link();
    ch32rv_irq_enable(CH32RV_ADC_IRQN);
    _begun = true;
    return true;
#else
    (void)pins;
    (void)count;
    return false;
#endif
}

bool CH32RVAdcInjected::simultaneous(const pin_size_t *pins2, uint8_t count)
{
#if defined(INJECTED_READY) && defined(CH32RV_ADC_DUAL_INJECTED_SIMULTANEOUS)
    if (!_begun || !pins2 || count != _count || ch32rv_timer_lease_valid(&_lease)) {
        return false;
    }
    uint8_t channels[4];
    for (uint8_t i = 0; i < count; i++) {
        const pin_size_t pad = CH32RV_PIN_RESOLVE(pins2[i]);
        const unsigned channel = CH32RV_PIN_TO_ADC_CHANNEL(pad);
        if (channel == NOT_AN_ANALOG_PIN) {
            return false;
        }
#ifdef CH32RV_PIN_TO_ADC_INSTANCE
        if (CH32RV_PIN_TO_ADC_INSTANCE(pad) != 2) {
            return false;                    /* X305 / X315: ADC2's own pads */
        }
#else
        if (channel == _channels[i]) {
            return false;                    /* one channel on both at once: the manual forbids it */
        }
#endif
        channels[i] = (uint8_t)channel;
        (void)analogRead(pins2[i]);          /* the pad in analog mode, ADC1's regular group only */
    }
    ch32rv_adc2_begin();
    /* ADC2: the same round on its channels, software-started (the manual's slave), the longest sample time
     * as analogRead() left both */
    uint32_t jsqr = (uint32_t)(count - 1u) << 20;
    for (uint8_t i = 0; i < count; i++) {
        jsqr |= (uint32_t)channels[i] << (5u * (4u - count + i));
    }
    ADC2_JSQR = jsqr;
    CH32RV_ADC_CTLR1_AT(CH32RV_ADC2_BASE) |= CTLR1_SCAN;
    const uint32_t mask = ((1u << CH32RV_ADC_JEXTSEL_BITS) - 1u) << CH32RV_ADC_JEXTSEL_SHIFT;
    CH32RV_ADC_CTLR2_AT(CH32RV_ADC2_BASE) = (CH32RV_ADC_CTLR2_AT(CH32RV_ADC2_BASE) & ~mask) |
                                            (uint32_t)CH32RV_ADC_JSWSTART_VALUE << CH32RV_ADC_JEXTSEL_SHIFT |
                                            CTLR2_JEXTTRIG;
    CH32RV_ADC_STATR_AT(CH32RV_ADC2_BASE) = ~STATR_JEOC;
    CH32RV_ADC_CTLR1 = (CH32RV_ADC_CTLR1 & ~(0xFu << CH32RV_ADC_DUALMOD_SHIFT)) |
                       (CH32RV_ADC_DUAL_INJECTED_SIMULTANEOUS << CH32RV_ADC_DUALMOD_SHIFT);
    _dual = true;
    return true;
#else
    (void)pins2;
    (void)count;
    return false;
#endif
}

void CH32RVAdcInjected::end()
{
    if (!_begun) {
        return;
    }
    stop();
#if defined(INJECTED_READY) && defined(CH32RV_ADC_DUAL_INJECTED_SIMULTANEOUS)
    if (_dual) {
        CH32RV_ADC_CTLR1 &= ~(0xFu << CH32RV_ADC_DUALMOD_SHIFT);
        CH32RV_ADC_CTLR1_AT(CH32RV_ADC2_BASE) &= ~CTLR1_SCAN;
        CH32RV_ADC_CTLR2_AT(CH32RV_ADC2_BASE) &= ~CTLR2_JEXTTRIG;
        _dual = false;
    }
#endif
#if defined(INJECTED_READY)
    CH32RV_ADC_CTLR1 &= ~(CTLR1_JEOCIE | CTLR1_SCAN);
    ch32rv_irq_disable(CH32RV_ADC_IRQN);
    CH32RV_ADC_CTLR2 &= ~CTLR2_JEXTTRIG;
#endif
    active = nullptr;
    _begun = false;
}

bool CH32RVAdcInjected::convert(uint16_t *out)
{
#if defined(INJECTED_READY)
    if (!_begun || ch32rv_timer_lease_valid(&_lease)) {
        return false;
    }
    const uint32_t before = _rounds;
    CH32RV_ADC_CTLR2 |= CTLR2_JSWSTART;
    const uint32_t t0 = micros();
    while (_rounds == before) {
        if (micros() - t0 > 10000u) {
            return false;
        }
    }
    for (uint8_t i = 0; out && i < (_dual ? 2u * _count : _count); i++) {
        out[i] = _values[i];
    }
    return true;
#else
    (void)out;
    return false;
#endif
}

bool CH32RVAdcInjected::startTimer(uint32_t hz)
{
#if defined(INJECTED_READY) && defined(CH32RV_ADC_JTRIGGERS)
    if (!_begun || hz == 0u) {
        return false;
    }
    stop();
    static const uint8_t sources[][3] = CH32RV_ADC_JTRIGGERS;   /* timer, event (0 TRGO, n CCn), value */
    for (const auto &s : sources) {
        const CH32RVTimerCapability *cap = ch32rv_timer_capability_for(s[0]);
        if (!cap || s[1] > cap->channels) {
            continue;
        }
        const uint64_t cycles = ch32rv_timer_clock_hz(s[0]) / hz;
        const uint64_t span = cap->counter_bits == 32u ? 0x100000000ull : 0x10000ull;
        uint64_t prescale = (cycles + span - 1u) / span;
        if (prescale == 0u) {
            prescale = 1u;
        }
        if (prescale > 65536u || cycles < 2u) {
            continue;
        }
        const uint32_t steps = (uint32_t)(cycles / prescale);
        const CH32RVTimerRequest request = {s[0], CH32RV_TIMER_WHOLE,
                                            {(uint16_t)(prescale - 1u), steps - 1u, CH32RV_TIM_CTLR1_ARPE}};
        _lease = ch32rv_timer_try_acquire(&request, &_owner);
        if (!ch32rv_timer_lease_valid(&_lease) || !ch32rv_timer_apply_base(&_lease)) {
            ch32rv_timer_release(&_lease);
            continue;
        }
        const uint32_t base = cap->register_base;
        if (s[1] == 0u) {
            /* TRGO on each update */
            CH32RV_REG16(base + 0x04u) = (uint16_t)((CH32RV_REG16(base + 0x04u) & ~(7u << 4)) | 2u << 4);
        } else {
            /* the channel's compare halfway through each period (PWM 1, its output left off the pads) */
            const uint8_t ch = s[1];
            volatile uint16_t &mode = ch <= 2 ? CH32RV_TIM_CHCTLR1(base) : CH32RV_TIM_CHCTLR2(base);
            const unsigned shift = ((ch - 1u) & 1u) * 8u;
            mode = (uint16_t)((mode & ~(0xFFu << shift)) | (0x68u << shift));
            CH32RV_TIM_CHCVR(base, ch) = (uint16_t)(steps / 2u);
            CH32RV_TIM_CCER(base) |= (uint16_t)(1u << ((ch - 1u) * 4u));
            if (cap->kind == CH32RV_TIMER_KIND_ADVANCED) {
                CH32RV_TIM_BDTR(base) |= CH32RV_TIM_BDTR_MOE;
            }
        }
        source(s[2]);
        _rate = (uint32_t)(ch32rv_timer_clock_hz(s[0]) / (prescale * steps));
        ch32rv_timer_start(&_lease);
        return true;
    }
#else
    (void)hz;
#endif
    return false;
}

void CH32RVAdcInjected::stop()
{
    if (ch32rv_timer_lease_valid(&_lease)) {
        const CH32RVTimerCapability *cap = ch32rv_timer_capability_for(_lease.timer);
        ch32rv_timer_stop(&_lease);
        CH32RV_TIM_CCER(cap->register_base) = 0;
        CH32RV_REG16(cap->register_base + 0x04u) &= (uint16_t)~(7u << 4);
    }
    ch32rv_timer_release(&_lease);
    _rate = 0;
#if defined(INJECTED_READY)
    if (_begun) {
        source(CH32RV_ADC_JSWSTART_VALUE);
    }
#endif
}

void CH32RVAdcInjected::onRound(Callback callback)
{
    noInterrupts();
    _callback = callback;
    interrupts();
}

uint16_t CH32RVAdcInjected::value(uint8_t i) const
{
    return i < (_dual ? 2u * _count : _count) ? _values[i] : 0;
}

uint8_t CH32RVAdcInjected::bits() const
{
    return CH32RV_ADC_BITS;
}
