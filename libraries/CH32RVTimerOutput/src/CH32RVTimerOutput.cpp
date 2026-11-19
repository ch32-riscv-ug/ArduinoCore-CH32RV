#include "CH32RVTimerOutput.h"

#include "TimerOutputCommon.h"
#include "internal/ch32rv_irq.h"
#include "internal/ch32rv_registers.h"

/* The second dead time off (setDeadTime(ns), end()). */
static inline void dt2Off(uint32_t base)
{
#if defined(CH32RV_TIM_DT2_OFFSET)
    volatile uint16_t &aux = *reinterpret_cast<volatile uint16_t *>(base + CH32RV_TIM_DT2_OFFSET);
    aux = (uint16_t)(aux & ~(CH32RV_TIM_DT2_RISING | CH32RV_TIM_DT2_FALLING));
#else
    (void)base;
#endif
}

using namespace ch32rv_timer_output;

/* ---- CH32RVComplementaryPwm ---- */

bool CH32RVComplementaryPwm::begin(int pin, int pinN, uint32_t frequency, uint8_t resolution)
{
    end();
    if (frequency == 0u || resolution < 1u || resolution > 16u) {
        return false;
    }
    CH32RVTimerPad p, n;
    for (uint8_t i = 0; ch32rv_timer_pad_find((uint32_t)pin, CH32RV_TIMER_ROLE(CH32RV_TIMER_PAD_CH), i, &p); i++) {
        for (uint8_t j = 0; ch32rv_timer_pad_find((uint32_t)pinN, CH32RV_TIMER_ROLE(CH32RV_TIMER_PAD_CHN), j, &n); j++) {
            if (p.channel == n.channel && ch32rv_timer_pads_together(&p, &n) && begin(p, n, frequency, resolution)) {
                return true;
            }
        }
    }
    return false;
}

bool CH32RVComplementaryPwm::begin(const CH32RVTimerPad &pad, const CH32RVTimerPad &padN, uint32_t frequency,
                                   uint8_t resolution)
{
    const CH32RVTimerCapability *cap = ch32rv_timer_capability_for(pad.timer);
    uint32_t prescale, steps;
    if (!cap || cap->kind != CH32RV_TIMER_KIND_ADVANCED ||
        !split(ch32rv_timer_clock_hz(pad.timer) / frequency, cap->counter_bits == 32u, prescale, steps)) {
        return false;
    }
    const CH32RVTimerRequest request = {
        pad.timer, CH32RV_TIMER_WHOLE,
        {(uint16_t)(prescale - 1u), steps - 1u, CH32RV_TIM_CTLR1_ARPE}
    };
    _lease = ch32rv_timer_try_acquire(&request, &_owner);
    if (!ch32rv_timer_lease_valid(&_lease) || !ch32rv_timer_apply_base(&_lease)) {
        ch32rv_timer_release(&_lease);
        return false;
    }
    _pad = pad;
    _padN = padN;
    _base = cap->register_base;
    _steps = steps;
    _bits = resolution;
    _break = false;
    _live = false;                  /* both idle (low) until the first write() */
    if (_dt2Edge) {
        /* asked for before begin(): the clock is known now */
        const uint32_t longer = _deadAsk, shorter = _dead2Ask;
        setDeadTime(_dt2Edge == 1 ? shorter : longer, _dt2Edge == 1 ? longer : shorter);
    } else if (_deadAsk) {
        setDeadTime(_deadAsk);      /* asked for before begin(): the clock is known now */
    }
    const uint8_t ch = pad.channel;
    channelMode(_base, ch, (uint8_t)(OCM_PWM1 << 4 | OCPE));
    setCompare(_base, ch, 0, false);
    /* Idle (MOE off) both low: OISx and OISxN 0. */
    TIM_CTLR2(_base) &= (uint16_t)~(3u << (8u + 2u * (ch - 1u)));
    channelEnable(_base, ch, 0x5);  /* CCxE | CCxNE, both active high */
    applyBdtr();
    ch32rv_timer_pad_connect(&_pad, true);
    ch32rv_timer_pad_connect(&_padN, true);
    ch32rv_timer_start(&_lease);
    return true;
}

void CH32RVComplementaryPwm::applyBdtr()
{
    if (!isAttached()) {
        return;
    }
    uint16_t bdtr = (uint16_t)(_dtg | BDTR_OSSI | BDTR_OSSR | (_live ? CH32RV_TIM_BDTR_MOE : 0u));
    if (_break) {
        bdtr |= (uint16_t)(BDTR_BKE | (_breakHigh ? BDTR_BKP : 0u));
    }
    CH32RV_TIM_INTFR(_base) = (uint16_t)~INT_BIF;
    CH32RV_TIM_BDTR(_base) = bdtr;
}

void CH32RVComplementaryPwm::end()
{
    if (isAttached()) {
        ch32rv_timer_stop(&_lease);
        CH32RV_TIM_BDTR(_base) = 0;
        CH32RV_TIM_CCER(_base) = 0;
        dt2Off(_base);
        padLow(_pad.pin);
        padLow(_padN.pin);
    }
    _live = false;
    ch32rv_timer_release(&_lease);
}

bool CH32RVComplementaryPwm::write(uint32_t duty)
{
    if (!isAttached()) {
        return false;
    }
    const uint32_t full = 1u << _bits;
    if (duty > full) {
        duty = full;
    }
    /* full is a power of two; at 100 % the compare is past the period's end. */
    setCompare(_base, _pad.channel, (uint32_t)(((uint64_t)duty * _steps) >> _bits), false);
    if (!_live) {
        /* The first duty: loaded now (UG, the period restarting) and only then
         * the outputs on, so the bridge starts on it - not on a part period
         * of begin()'s 0, CHxN high (V205, 1.6 us between begin() and write()). */
        CH32RV_TIM_SWEVGR(_base) = SWEVGR_UG;
        _live = true;
        applyBdtr();
    }
    return true;
}

uint32_t CH32RVComplementaryPwm::setDeadTime(uint32_t nanoseconds)
{
    _deadAsk = nanoseconds;
    _dt2Edge = 0;
    _dead2Ns = 0;
    if (isAttached()) {
        dt2Off(_base);
    }
    const uint32_t clock = ch32rv_timer_clock_hz(isAttached() ? _pad.timer : 1u);
    uint32_t ticks;
    _dtg = deadTimeField(clock, nanoseconds, ticks);
    _deadNs = (uint32_t)(((uint64_t)ticks * 1000000000u + clock / 2u) / clock);
    if (nanoseconds == 0u) {
        _deadNs = 0;
    }
    applyBdtr();
    return _deadNs;
}

bool CH32RVComplementaryPwm::setDeadTime(uint32_t risingNs, uint32_t fallingNs)
{
    if (risingNs == fallingNs) {
        setDeadTime(risingNs);
        return true;
    }
#if defined(CH32RV_TIM_DT2_OFFSET)
    const bool risingShort = risingNs < fallingNs;
    const uint32_t longer = risingShort ? fallingNs : risingNs, shorter = risingShort ? risingNs : fallingNs;
    setDeadTime(longer);
    _dead2Ask = shorter;
    _dt2Edge = risingShort ? 1u : 2u;
    /* The second dead time counts in the first's steps (Tdtg: 1, 2, 8 or 16
     * clocks by DTG's top bits), more than 0 and fewer than DTG's. */
    const uint32_t clock = ch32rv_timer_clock_hz(isAttached() ? _pad.timer : 1u);
    const uint8_t top = _dtg >> 5;
    const uint32_t step = top < 4u ? 1u : top < 6u ? 2u : top == 6u ? 8u : 16u;
    const uint32_t longTicks = (uint32_t)(((uint64_t)_deadNs * clock + 500000000u) / 1000000000u);
    const uint32_t want = (uint32_t)(((uint64_t)shorter * clock + 999999999u) / 1000000000u);
    uint32_t units = (want + step - 1u) / step;
    const uint32_t most = longTicks / step > 1u ? longTicks / step - 1u : 0u;
    if (most == 0u) {
        return false;                  /* the longer is too short to have a shorter */
    }
    units = units < 1u ? 1u : units > most ? most : units;
    if (units > 0xFFu) {
        units = 0xFFu;
    }
    _dead2Ns = (uint32_t)(((uint64_t)units * step * 1000000000u + clock / 2u) / clock);
    if (isAttached()) {
        volatile uint16_t &aux = *reinterpret_cast<volatile uint16_t *>(_base + CH32RV_TIM_DT2_OFFSET);
        aux = (uint16_t)((aux & ~(0xFFu << CH32RV_TIM_DT2_SHIFT) & ~(CH32RV_TIM_DT2_RISING | CH32RV_TIM_DT2_FALLING)) |
                         (units << CH32RV_TIM_DT2_SHIFT) |
                         (risingShort ? CH32RV_TIM_DT2_RISING : CH32RV_TIM_DT2_FALLING));
    }
    return true;
#else
    return false;
#endif
}

bool CH32RVComplementaryPwm::attachBreak(int pin, int activeLevel, uint8_t mode)
{
    if (!isAttached()) {
        return false;
    }
    CH32RVTimerPad b;
    for (uint8_t i = 0; ch32rv_timer_pad_find((uint32_t)pin, CH32RV_TIMER_ROLE(CH32RV_TIMER_PAD_BKIN), i, &b); i++) {
        if (ch32rv_timer_pads_together(&_pad, &b)) {
            pinMode(b.pin, mode);
            ch32rv_timer_pad_connect(&b, false);
            _break = true;
            _breakHigh = activeLevel == HIGH;
            applyBdtr();
            return true;
        }
    }
    return false;
}

void CH32RVComplementaryPwm::detachBreak()
{
    _break = false;
    applyBdtr();
}

bool CH32RVComplementaryPwm::breakActive()
{
    return isAttached() && (CH32RV_TIM_INTFR(_base) & INT_BIF) != 0u;
}

bool CH32RVComplementaryPwm::resume()
{
    if (!isAttached()) {
        return false;
    }
    /* While the break input is active the hardware keeps MOE clear. */
    CH32RV_TIM_INTFR(_base) = (uint16_t)~INT_BIF;
    if (!_live) {
        return true;                /* no write() yet: the outputs stay idle */
    }
    CH32RV_TIM_BDTR(_base) |= CH32RV_TIM_BDTR_MOE;
    return (CH32RV_TIM_BDTR(_base) & CH32RV_TIM_BDTR_MOE) != 0u;
}

/* ---- CH32RVOnePulse ---- */

bool CH32RVOnePulse::begin(int pin, int polarity, int triggerPin, int triggerEdge)
{
    end();
    CH32RVTimerPad p, t;
    for (uint8_t i = 0; ch32rv_timer_pad_find((uint32_t)pin, CH32RV_TIMER_ROLE(CH32RV_TIMER_PAD_CH), i, &p); i++) {
        if (triggerPin < 0) {
            if (begin(p, nullptr, polarity, triggerEdge)) {
                return true;
            }
            continue;
        }
        for (uint8_t j = 0; ch32rv_timer_pad_find((uint32_t)triggerPin, CH32RV_TIMER_ROLE(CH32RV_TIMER_PAD_CH), j, &t);
             j++) {
            if ((t.channel == 1 || t.channel == 2) && t.channel != p.channel &&
                ch32rv_timer_pads_together(&p, &t) && begin(p, &t, polarity, triggerEdge)) {
                return true;
            }
        }
    }
    return false;
}

bool CH32RVOnePulse::begin(const CH32RVTimerPad &pad, const CH32RVTimerPad *trigger, int polarity, int triggerEdge)
{
    const CH32RVTimerCapability *cap = ch32rv_timer_capability_for(pad.timer);
    if (!cap) {
        return false;
    }
    _wide = cap->counter_bits == 32u;
    const CH32RVTimerRequest request = {
        pad.timer, CH32RV_TIMER_WHOLE,
        {0, _wide ? 0xFFFFFFFFu : 0xFFFFu, CTLR1_OPM}
    };
    _lease = ch32rv_timer_try_acquire(&request, &_owner);
    if (!ch32rv_timer_lease_valid(&_lease) || !ch32rv_timer_apply_base(&_lease)) {
        ch32rv_timer_release(&_lease);
        return false;
    }
    _base = cap->register_base;
    _channel = pad.channel;
    _pin = pad.pin;
    /* Until setPulse(): the output held inactive. PWM mode 2 then: inactive
     * below the compare (the delay), active from it to the end. */
    channelMode(_base, _channel, (uint8_t)(OCM_FORCE_INACTIVE << 4));
    channelEnable(_base, _channel, polarity == LOW ? 0x3 : 0x1);
    if (trigger) {
        channelMode(_base, trigger->channel, 1);                      /* CCxS: its own input */
        channelEnable(_base, trigger->channel, triggerEdge == FALLING ? 0x2 : 0x0);
        CH32RV_TIM_SMCFGR(_base) = (uint16_t)((trigger->channel == 1 ? TS_TI1FP1 : TS_TI2FP2) | SMS_TRIGGER);
        pinMode(trigger->pin, INPUT);
        ch32rv_timer_pad_connect(trigger, false);
    }
    if (cap->kind == CH32RV_TIMER_KIND_ADVANCED) {
        CH32RV_TIM_BDTR(_base) |= CH32RV_TIM_BDTR_MOE;
    }
    ch32rv_timer_pad_connect(&pad, true);
    return true;
}

void CH32RVOnePulse::end()
{
    if (isAttached()) {
        CH32RV_TIM_SMCFGR(_base) = 0;
        CH32RV_TIM_CCER(_base) = 0;
        CH32RV_TIM_CHCTLR1(_base) = 0;
        CH32RV_TIM_CHCTLR2(_base) = 0;
        padLow(_pin);
    }
    ch32rv_timer_release(&_lease);
}

bool CH32RVOnePulse::setPulse(uint32_t delayMicros, uint32_t widthMicros)
{
    if (!isAttached() || busy() || widthMicros == 0u) {
        return false;
    }
    const uint8_t timer = _lease.timer;
    uint32_t prescale, steps;
    if (!split(clocksIn(timer, delayMicros) + clocksIn(timer, widthMicros), _wide, prescale, steps)) {
        return false;
    }
    uint32_t delay = (uint32_t)((clocksIn(timer, delayMicros) + prescale / 2u) / prescale);
    const uint32_t width = (uint32_t)((clocksIn(timer, widthMicros) + prescale / 2u) / prescale);
    if (delay == 0u) {
        delay = 1u;                 /* the compare at 0 would hold the output active */
    }
    if (width == 0u) {
        return false;
    }
    CH32RV_TIM_PSC(_base) = (uint16_t)(prescale - 1u);
    if (_wide) {
        CH32RV_TIM_ATRLR32(_base) = delay + width - 1u;
    } else {
        CH32RV_TIM_ATRLR(_base) = (uint16_t)(delay + width - 1u);
    }
    setCompare(_base, _channel, delay, _wide);
    CH32RV_TIM_SWEVGR(_base) = SWEVGR_UG;          /* the prescaler loaded, the count at 0 */
    channelMode(_base, _channel, (uint8_t)(OCM_PWM2 << 4));
    return true;
}

bool CH32RVOnePulse::fire()
{
    if (!isAttached() || busy()) {
        return false;
    }
    CH32RV_TIM_CTLR1(_base) |= CH32RV_TIM_CTLR1_CEN;
    return true;
}

bool CH32RVOnePulse::busy()
{
    return isAttached() && (CH32RV_TIM_CTLR1(_base) & CH32RV_TIM_CTLR1_CEN) != 0u;
}

/* ---- CH32RVOutputCompare ---- */

bool CH32RVOutputCompare::begin(int pin, uint32_t periodMicros)
{
    end();
    CH32RVTimerPad p;
    for (uint8_t i = 0; ch32rv_timer_pad_find((uint32_t)pin, CH32RV_TIMER_ROLE(CH32RV_TIMER_PAD_CH), i, &p); i++) {
        if (begin(p, periodMicros)) {
            return true;
        }
    }
    return false;
}

bool CH32RVOutputCompare::begin(const CH32RVTimerPad &pad, uint32_t periodMicros)
{
    const CH32RVTimerCapability *cap = ch32rv_timer_capability_for(pad.timer);
    uint32_t prescale, steps;
    if (!cap || !split(clocksIn(pad.timer, periodMicros), cap->counter_bits == 32u, prescale, steps)) {
        return false;
    }
    _wide = cap->counter_bits == 32u;
    const CH32RVTimerRequest request = {
        pad.timer, CH32RV_TIMER_WHOLE,
        {(uint16_t)(prescale - 1u), steps - 1u, CH32RV_TIM_CTLR1_ARPE}
    };
    _lease = ch32rv_timer_try_acquire(&request, &_owner);
    if (!ch32rv_timer_lease_valid(&_lease) || !ch32rv_timer_apply_base(&_lease)) {
        ch32rv_timer_release(&_lease);
        return false;
    }
    _base = cap->register_base;
    _channel = pad.channel;
    _pin = pad.pin;
    _steps = steps;
    _tickHz = ch32rv_timer_clock_hz(pad.timer) / prescale;
    channelMode(_base, _channel, (uint8_t)(OCM_FORCE_INACTIVE << 4));
    channelEnable(_base, _channel, 0x1);
    if (cap->kind == CH32RV_TIMER_KIND_ADVANCED) {
        CH32RV_TIM_BDTR(_base) |= CH32RV_TIM_BDTR_MOE;
    }
    if (!ch32rv_timer_attach_channel_interrupt(&_lease, onInterrupt, this)) {
        ch32rv_timer_release(&_lease);
        return false;
    }
    if (_callback) {
        CH32RV_TIM_DMAINTENR(_base) |= (uint16_t)(1u << _channel);
    }
    ch32rv_timer_pad_connect(&pad, true);
    ch32rv_timer_start(&_lease);
    return true;
}

void CH32RVOutputCompare::end()
{
    if (isAttached()) {
        ch32rv_timer_detach_channel_interrupt(&_lease);
        CH32RV_TIM_CCER(_base) = 0;
        CH32RV_TIM_CHCTLR1(_base) = 0;
        CH32RV_TIM_CHCTLR2(_base) = 0;
        padLow(_pin);
    }
    ch32rv_timer_release(&_lease);
}

bool CH32RVOutputCompare::set(Mode mode, uint32_t atMicros)
{
    if (!isAttached() || mode > FORCE_ACTIVE) {
        return false;
    }
    const uint64_t at = (uint64_t)atMicros * _tickHz / 1000000u;
    if (at > _steps) {
        return false;
    }
    setCompare(_base, _channel, (uint32_t)at, _wide);
    channelMode(_base, _channel, (uint8_t)(mode << 4));
    return true;
}

void CH32RVOutputCompare::onMatch(Callback callback)
{
    const uint32_t irq = ch32rv_irq_save();
    _callback = callback;
    if (isAttached()) {
        const uint16_t bit = (uint16_t)(1u << _channel);
        if (callback) {
            CH32RV_TIM_INTFR(_base) = (uint16_t)~bit;
            CH32RV_TIM_DMAINTENR(_base) |= bit;
        } else {
            CH32RV_TIM_DMAINTENR(_base) &= (uint16_t)~bit;
        }
    }
    ch32rv_irq_restore(irq);
}

void CH32RVOutputCompare::onInterrupt(void *self)
{
    CH32RVOutputCompare *o = static_cast<CH32RVOutputCompare *>(self);
    const uint16_t bit = (uint16_t)(1u << o->_channel);
    if ((CH32RV_TIM_INTFR(o->_base) & bit) == 0u) {
        return;
    }
    CH32RV_TIM_INTFR(o->_base) = (uint16_t)~bit;
    if (o->_callback) {
        o->_callback();
    }
}
