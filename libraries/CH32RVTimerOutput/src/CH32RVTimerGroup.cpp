/* CH32RVPwmTimer, CH32RVPhaseShift, CH32RVSixStep: several outputs of one
 * timer (CH32RVTimerOutput.h). */
#include "CH32RVTimerOutput.h"

#include "TimerOutputCommon.h"

using namespace ch32rv_timer_output;

namespace {

constexpr uint16_t CTLR1_CMS_CENTER1 = 1u << 5;   /* up and down; compare flags on the way down */
constexpr uint16_t CTLR2_CCPC = 1u << 0;          /* CCxE, CCxNE, OCxM preloaded, moved at COM */
constexpr uint16_t SWEVGR_COMG = 1u << 5;
constexpr uint8_t OCM_TOGGLE = 3, OCM_FORCE_ACTIVE = 5;
constexpr uint16_t MMS_ENABLE = 1u << 4;          /* CTLR2: TRGO is the counter's enable */

/* Sets (or clears) one bit of a channel's CCER nibble: 0 CCxE, 1 CCxP, 2 CCxNE. */
void ccerBit(uint32_t base, uint8_t ch, unsigned bit, bool on)
{
    const uint16_t mask = (uint16_t)(1u << ((ch - 1u) * 4u + bit));
    CH32RV_TIM_CCER(base) = on ? (uint16_t)(CH32RV_TIM_CCER(base) | mask) : (uint16_t)(CH32RV_TIM_CCER(base) & ~mask);
}

bool acquire(const CH32RVTimerPad &pad, uint32_t prescale, uint32_t reload, uint16_t control,
             const CH32RVTimerOwner *owner, CH32RVTimerLease &lease)
{
    const CH32RVTimerRequest request = {pad.timer, CH32RV_TIMER_WHOLE, {(uint16_t)(prescale - 1u), reload, control}};
    lease = ch32rv_timer_try_acquire(&request, owner);
    if (!ch32rv_timer_lease_valid(&lease) || !ch32rv_timer_apply_base(&lease)) {
        ch32rv_timer_release(&lease);
        return false;
    }
    return true;
}

constexpr uint8_t ROLES = CH32RV_TIMER_ROLE(CH32RV_TIMER_PAD_CH) | CH32RV_TIMER_ROLE(CH32RV_TIMER_PAD_CHN);

}  // namespace

/* ---- CH32RVPwmTimer ---- */

bool CH32RVPwmTimer::begin(int pin, uint32_t frequency, uint8_t resolution, Align align, uint8_t timer)
{
    end();
    if (frequency == 0u || resolution < 1u || resolution > 16u) {
        return false;
    }
    CH32RVTimerPad p;
    for (uint8_t i = 0; ch32rv_timer_pad_find((uint32_t)pin, ROLES, i, &p); i++) {
        if ((timer == 0u || p.timer == timer) && begin(p, frequency, resolution, align)) {
            return true;
        }
    }
    return false;
}

bool CH32RVPwmTimer::begin(const CH32RVTimerPad &pad, uint32_t frequency, uint8_t resolution, Align align)
{
    const CH32RVTimerCapability *cap = ch32rv_timer_capability_for(pad.timer);
    if (!cap) {
        return false;
    }
    _advanced = cap->kind == CH32RV_TIMER_KIND_ADVANCED;
    _wide = cap->counter_bits == 32u;
    if (pad.role == CH32RV_TIMER_PAD_CHN && !_advanced) {
        return false;
    }
    /* Center-aligned: a period is the counter up to the reload and back. */
    const uint64_t cycles = ch32rv_timer_clock_hz(pad.timer) / frequency / (align == CENTER ? 2u : 1u);
    uint32_t prescale, steps;
    if (!split(cycles, _wide, prescale, steps)) {
        return false;
    }
    const uint32_t reload = align == CENTER ? steps : steps - 1u;
    if (!acquire(pad, prescale, reload,
                 (uint16_t)(CH32RV_TIM_CTLR1_ARPE | (align == CENTER ? CTLR1_CMS_CENTER1 : 0u)), &_owner, _lease)) {
        return false;
    }
    _timer = pad.timer;
    _base = cap->register_base;
    _steps = steps;
    _bits = resolution;
    _count = 0;
    if (_advanced) {
        CH32RV_TIM_BDTR(_base) = (uint16_t)(BDTR_OSSR | CH32RV_TIM_BDTR_MOE);
    }
    /* The pad last: a stopped timer's output keeps what its last clock left
     * (an earlier user's PWM high), and only a running counter brings the
     * reference to the new compare. UG loads the compare 0, the counter
     * moves once, and then the pad is the timer's - connected first, V006
     * held PC0 high the ~11 us to the start (center-aligned after an
     * edge-aligned run, signals::test_pwm_timer, 2026-11-21). */
    connect(pad, false);
    CH32RV_TIM_SWEVGR(_base) = SWEVGR_UG;
    ch32rv_timer_start(&_lease);
    for (uint32_t spin = 0; spin < 100000u && (_wide ? CH32RV_TIM_CNT32(_base) : CH32RV_TIM_CNT(_base)) == 0; spin++) {
    }
    ch32rv_timer_pad_connect(&pad, true);
    return true;
}

bool CH32RVPwmTimer::connect(const CH32RVTimerPad &pad, bool pin)
{
    if (_count >= sizeof _pads / sizeof _pads[0]) {
        return false;
    }
    bool shared = false;                   /* the channel already runs for another pad (CHx and CHxN) */
    for (uint8_t i = 0; i < _count; i++) {
        shared |= _pads[i].channel == pad.channel;
    }
    if (!shared) {
        channelMode(_base, pad.channel, (uint8_t)(OCM_PWM1 << 4 | OCPE));
        setCompare(_base, pad.channel, 0, _wide);
    }
    ccerBit(_base, pad.channel, pad.role == CH32RV_TIMER_PAD_CHN ? 2u : 0u, true);
    if (pin) {
        ch32rv_timer_pad_connect(&pad, true);
    }
    _pads[_count++] = pad;
    return true;
}

bool CH32RVPwmTimer::attach(int pin)
{
    if (!isAttached() || find(pin)) {
        return isAttached();
    }
    CH32RVTimerPad p;
    for (uint8_t i = 0; ch32rv_timer_pad_find((uint32_t)pin, ROLES, i, &p); i++) {
        if (p.timer == _timer && ch32rv_timer_pads_together(&_pads[0], &p) &&
            (p.role == CH32RV_TIMER_PAD_CH || _advanced)) {
            return connect(p);
        }
    }
    return false;
}

const CH32RVTimerPad *CH32RVPwmTimer::find(int pin) const
{
    for (uint8_t i = 0; i < _count; i++) {
        if (_pads[i].pin == (uint8_t)CH32RV_PIN_RESOLVE(pin)) {
            return &_pads[i];
        }
    }
    return nullptr;
}

bool CH32RVPwmTimer::write(int pin, uint32_t duty)
{
    const CH32RVTimerPad *p = isAttached() ? find(pin) : nullptr;
    if (!p) {
        return false;
    }
    const uint32_t full = 1u << _bits;
    if (duty >= full) {
        /* past the period's end: always active (center: above the reload) */
        setCompare(_base, p->channel, _steps + 1u, _wide);
        return true;
    }
    setCompare(_base, p->channel, (uint32_t)(((uint64_t)duty * _steps) >> _bits), _wide);
    return true;
}

/* Stopped at 0, the slave mode off. UG for the prescaler and the preloads, and the counter written as well: a
 * stopped counter took no UG (V203: CNT stayed where it stopped). */
void CH32RVPwmTimer::restartCounter()
{
    ch32rv_timer_stop(&_lease);
    CH32RV_TIM_SMCFGR(_base) = 0;
    CH32RV_TIM_SWEVGR(_base) = SWEVGR_UG;
    /* The outputs low while the counter waits. A stopped timer's outputs keep
     * what its last clock left there, and the UG above, at 0 below each
     * compare, can leave them active: V006 held PC0 high while startOn()
     * waited, and neither forcing the reference inactive nor moving the count
     * changed the pad until the counter ran (registers read through the
     * probe, 2026-11-20). So: forced inactive, a few counts run, stopped, PWM
     * mode 1 again - the pads stay low until the counter starts. */
    for (uint8_t k = 0; k < _count; k++) {
        channelMode(_base, _pads[k].channel, (uint8_t)(OCM_FORCE_INACTIVE << 4 | OCPE));
    }
    CH32RV_TIM_CTLR1(_base) |= CH32RV_TIM_CTLR1_CEN;
    for (uint32_t spin = 0; spin < 100000u && (_wide ? CH32RV_TIM_CNT32(_base) : CH32RV_TIM_CNT(_base)) == 0; spin++) {
    }
    CH32RV_TIM_CTLR1(_base) &= (uint16_t)~CH32RV_TIM_CTLR1_CEN;
    for (uint8_t k = 0; k < _count; k++) {
        channelMode(_base, _pads[k].channel, (uint8_t)(OCM_PWM1 << 4 | OCPE));
    }
    if (_wide) {
        CH32RV_TIM_CNT32(_base) = 0;
    } else {
        CH32RV_TIM_CNT(_base) = 0;
    }
}

bool CH32RVPwmTimer::startWith(CH32RVPwmTimer &master)
{
    if (!isAttached() || !master.isAttached() || &master == this) {
        return false;
    }
    const int itr = ch32rv_timer_trigger_from(_timer, master._timer);
    if (itr < 0) {
        return false;
    }
    master.restartCounter();
    restartCounter();
    TIM_CTLR2(master._base) = (uint16_t)((TIM_CTLR2(master._base) & ~(7u << 4)) | MMS_ENABLE);
    CH32RV_TIM_SMCFGR(_base) = (uint16_t)((unsigned)itr << 4 | SMS_TRIGGER);
    ch32rv_timer_start(&master._lease);     /* its enable starts this one */
    return true;
}

bool CH32RVPwmTimer::startOn(int pin, int edge)
{
    if (!isAttached() || find(pin)) {
        return false;
    }
    CH32RVTimerPad t;
    for (uint8_t i = 0; ch32rv_timer_pad_find((uint32_t)pin, CH32RV_TIMER_ROLE(CH32RV_TIMER_PAD_CH), i, &t); i++) {
        bool used = false;
        for (uint8_t k = 0; k < _count; k++) {
            used |= _pads[k].channel == t.channel;
        }
        if (used || t.timer != _timer || (t.channel != 1 && t.channel != 2) || !ch32rv_timer_pads_together(&_pads[0], &t)) {
            continue;
        }
        restartCounter();
        channelMode(_base, t.channel, 1);                      /* CCxS: its own input */
        ccerBit(_base, t.channel, 1u, edge == FALLING);
        pinMode(t.pin, INPUT);
        ch32rv_timer_pad_connect(&t, false);
        CH32RV_TIM_SMCFGR(_base) = (uint16_t)((t.channel == 1 ? TS_TI1FP1 : TS_TI2FP2) | SMS_TRIGGER);
        return true;                                           /* the edge sets CEN */
    }
    return false;
}

void CH32RVPwmTimer::end()
{
    if (isAttached()) {
        ch32rv_timer_stop(&_lease);
        CH32RV_TIM_SMCFGR(_base) = 0;
        TIM_CTLR2(_base) &= (uint16_t)~(7u << 4);
        CH32RV_TIM_CCER(_base) = 0;
        if (_advanced) {
            CH32RV_TIM_BDTR(_base) = 0;
        }
        for (uint8_t i = 0; i < _count; i++) {
            padLow(_pads[i].pin);
        }
    }
    _count = 0;
    ch32rv_timer_release(&_lease);
}

/* ---- CH32RVPhaseShift ---- */

bool CH32RVPhaseShift::begin(int pin, uint32_t frequency, uint16_t degrees)
{
    end();
    if (frequency == 0u) {
        return false;
    }
    CH32RVTimerPad p;
    for (uint8_t i = 0; ch32rv_timer_pad_find((uint32_t)pin, CH32RV_TIMER_ROLE(CH32RV_TIMER_PAD_CH), i, &p); i++) {
        if (begin(p, frequency, degrees)) {
            return true;
        }
    }
    return false;
}

bool CH32RVPhaseShift::begin(const CH32RVTimerPad &pad, uint32_t frequency, uint16_t degrees)
{
    const CH32RVTimerCapability *cap = ch32rv_timer_capability_for(pad.timer);
    if (!cap) {
        return false;
    }
    _wide = cap->counter_bits == 32u;
    _advanced = cap->kind == CH32RV_TIMER_KIND_ADVANCED;
    /* A toggle each counter period: the counter at twice the frequency. */
    uint32_t prescale, steps;
    if (!split(ch32rv_timer_clock_hz(pad.timer) / frequency / 2u, _wide, prescale, steps) ||
        !acquire(pad, prescale, steps - 1u, CH32RV_TIM_CTLR1_ARPE, &_owner, _lease)) {
        return false;
    }
    _base = cap->register_base;
    _steps = steps;
    _pads[0] = pad;
    _degrees[0] = (uint16_t)(degrees % 360u);
    _count = 1;
    if (_advanced) {
        CH32RV_TIM_BDTR(_base) = (uint16_t)(BDTR_OSSR | CH32RV_TIM_BDTR_MOE);
    }
    ch32rv_timer_pad_connect(&pad, true);
    restart();
    return true;
}

bool CH32RVPhaseShift::attach(int pin, uint16_t degrees)
{
    if (!isAttached()) {
        return false;
    }
    const uint8_t want = (uint8_t)CH32RV_PIN_RESOLVE(pin);
    for (uint8_t i = 0; i < _count; i++) {
        if (_pads[i].pin == want) {
            return setPhase(pin, degrees);
        }
    }
    CH32RVTimerPad p;
    for (uint8_t i = 0; _count < 4u && ch32rv_timer_pad_find((uint32_t)pin, CH32RV_TIMER_ROLE(CH32RV_TIMER_PAD_CH), i, &p);
         i++) {
        bool used = false;
        for (uint8_t k = 0; k < _count; k++) {
            used |= _pads[k].channel == p.channel;
        }
        if (!used && p.timer == _pads[0].timer && ch32rv_timer_pads_together(&_pads[0], &p)) {
            _pads[_count] = p;
            _degrees[_count++] = (uint16_t)(degrees % 360u);
            ch32rv_timer_pad_connect(&p, true);
            restart();
            return true;
        }
    }
    return false;
}

bool CH32RVPhaseShift::setPhase(int pin, uint16_t degrees)
{
    const uint8_t want = (uint8_t)CH32RV_PIN_RESOLVE(pin);
    for (uint8_t i = 0; isAttached() && i < _count; i++) {
        if (_pads[i].pin == want) {
            _degrees[i] = (uint16_t)(degrees % 360u);
            restart();
            return true;
        }
    }
    return false;
}

/* Every output low (forced), its toggle point and polarity set, the counter
 * from 0: they start together. */
void CH32RVPhaseShift::restart()
{
    ch32rv_timer_stop(&_lease);
    /* From the channels' reset state, as begin() finds them: a toggle channel that had been running kept its
     * level through the forced one and came back inverted (X035). */
    CH32RV_TIM_CCER(_base) = 0;
    CH32RV_TIM_CHCTLR1(_base) = 0;
    CH32RV_TIM_CHCTLR2(_base) = 0;
    CH32RV_TIM_SWEVGR(_base) = SWEVGR_UG;
    for (uint8_t i = 0; i < _count; i++) {
        const uint8_t ch = _pads[i].channel;
        const uint16_t d = _degrees[i];
        const bool invert = d >= 180u;
        uint32_t at = (uint32_t)((uint64_t)(invert ? d - 180u : d) * _steps / 180u);
        /* Never 0: when a compare of 0 first matches (as the counter starts, or at its first wrap a counter period
         * later) differs between chips; 1 is a tick later for every such channel alike. */
        if (at == 0u) {
            at = 1u;
        }
        channelMode(_base, ch, (uint8_t)(OCM_FORCE_INACTIVE << 4));
        setCompare(_base, ch, at, _wide);
        ccerBit(_base, ch, 1u, invert);
        ccerBit(_base, ch, 0u, true);
        channelMode(_base, ch, (uint8_t)(OCM_TOGGLE << 4));
    }
    /* Toggle mode does not always start from the forced level (X035: a channel that had been running came back
     * at the other one): each pad says which it took, and its polarity makes that the inactive level. */
    delayMicroseconds(2);
    for (uint8_t i = 0; i < _count; i++) {
        const bool invert = _degrees[i] >= 180u;
        if ((digitalRead(_pads[i].pin) != LOW) != invert) {
            ccerBit(_base, _pads[i].channel, 1u, !invert);
        }
    }
    /* One tick before the wrap: every channel's first match comes after the counter has run (a match at the
     * first tick after the update was lost on X035). */
    if (_wide) {
        CH32RV_TIM_CNT32(_base) = _steps - 1u;
    } else {
        CH32RV_TIM_CNT(_base) = (uint16_t)(_steps - 1u);
    }
    ch32rv_timer_start(&_lease);
}

void CH32RVPhaseShift::end()
{
    if (isAttached()) {
        ch32rv_timer_stop(&_lease);
        CH32RV_TIM_CCER(_base) = 0;
        if (_advanced) {
            CH32RV_TIM_BDTR(_base) = 0;
        }
        for (uint8_t i = 0; i < _count; i++) {
            padLow(_pads[i].pin);
        }
    }
    _count = 0;
    ch32rv_timer_release(&_lease);
}

/* ---- CH32RVSixStep ---- */

bool CH32RVSixStep::begin(int uHigh, int uLow, int vHigh, int vLow, int wHigh, int wLow, uint32_t frequency,
                          uint8_t resolution)
{
    end();
    if (frequency == 0u || resolution < 1u || resolution > 16u) {
        return false;
    }
    const int high[3] = {uHigh, vHigh, wHigh}, low[3] = {uLow, vLow, wLow};
    CH32RVTimerPad first, h[3], l[3];
    /* Each route of U's high side, with the other five pads on it. */
    for (uint8_t i = 0; ch32rv_timer_pad_find((uint32_t)uHigh, CH32RV_TIMER_ROLE(CH32RV_TIMER_PAD_CH), i, &first); i++) {
        bool all = true;
        for (uint8_t k = 0; k < 3 && all; k++) {
            bool hit = false, lowHit = false;
            for (uint8_t j = 0; !hit && ch32rv_timer_pad_find((uint32_t)high[k], CH32RV_TIMER_ROLE(CH32RV_TIMER_PAD_CH),
                                                                j, &h[k]); j++) {
                hit = ch32rv_timer_pads_together(&first, &h[k]);
            }
            for (uint8_t j = 0; hit && !lowHit &&
                                ch32rv_timer_pad_find((uint32_t)low[k], CH32RV_TIMER_ROLE(CH32RV_TIMER_PAD_CHN), j, &l[k]);
                 j++) {
                lowHit = l[k].channel == h[k].channel && ch32rv_timer_pads_together(&first, &l[k]);
            }
            all = hit && lowHit;
        }
        if (!all || h[0].channel == h[1].channel || h[1].channel == h[2].channel || h[0].channel == h[2].channel) {
            continue;
        }
        const CH32RVTimerCapability *cap = ch32rv_timer_capability_for(h[0].timer);
        uint32_t prescale, steps;
        if (!cap || cap->kind != CH32RV_TIMER_KIND_ADVANCED ||
            !split(ch32rv_timer_clock_hz(h[0].timer) / frequency, false, prescale, steps) ||
            !acquire(h[0], prescale, steps - 1u, CH32RV_TIM_CTLR1_ARPE, &_owner, _lease)) {
            continue;
        }
        _base = cap->register_base;
        _steps = steps;
        _bits = resolution;
        for (uint8_t k = 0; k < 3; k++) {
            _pads[2 * k] = h[k];
            _pads[2 * k + 1] = l[k];
            channelMode(_base, h[k].channel, (uint8_t)(OCM_FORCE_INACTIVE << 4 | OCPE));
            setCompare(_base, h[k].channel, 0, false);
        }
        CH32RV_TIM_CCER(_base) = 0;
        TIM_CTLR2(_base) = (uint16_t)((TIM_CTLR2(_base) & ~0x3F00u) | CTLR2_CCPC);   /* idle low, preloaded */
        CH32RV_TIM_BDTR(_base) = (uint16_t)(_dtg | BDTR_OSSI | BDTR_OSSR | CH32RV_TIM_BDTR_MOE);
        for (uint8_t k = 0; k < 3; k++) {
            channelEnable(_base, h[k].channel, 0x1);                /* off until the first step: driven low */
        }
        CH32RV_TIM_SWEVGR(_base) = SWEVGR_COMG;
        for (uint8_t k = 0; k < 6; k++) {
            ch32rv_timer_pad_connect(&_pads[k], true);
        }
        _step = 0xFF;
        ch32rv_timer_start(&_lease);
        return true;
    }
    return false;
}

bool CH32RVSixStep::write(uint32_t duty)
{
    if (!isAttached()) {
        return false;
    }
    const uint32_t full = 1u << _bits;
    const uint32_t c = duty >= full ? _steps : (uint32_t)(((uint64_t)duty * _steps) >> _bits);
    for (uint8_t k = 0; k < 3; k++) {
        setCompare(_base, _pads[2 * k].channel, c, false);
    }
    return true;
}

bool CH32RVSixStep::step(uint8_t n)
{
    if (!isAttached()) {
        return false;
    }
    /* Per step: the phase whose high side runs the PWM, the one whose low side is on. */
    static const uint8_t pwm[6] = {0, 0, 1, 1, 2, 2}, low[6] = {1, 2, 2, 0, 0, 1};
    for (uint8_t k = 0; k < 3; k++) {
        const uint8_t ch = _pads[2 * k].channel;
        /* Off: OCxREF forced inactive with CCxE alone, so OSSR drives CHxN to its
         * inactive level too. CCxE and CCxNE both clear disable the outputs
         * whatever OSSR says, and the pads float (V205 2026-11-20: the off
         * phases' pads showed noise pulses and ended high). */
        uint8_t mode = OCM_FORCE_INACTIVE, ccer = 0x1;
        if (n < 6u && pwm[n] == k) {
            mode = OCM_PWM1;
            ccer = 0x1;                                          /* CCxE: the high side */
        } else if (n < 6u && low[n] == k) {
            mode = OCM_FORCE_ACTIVE;
            ccer = 0x4;                                          /* CCxNE alone: CHxN follows OCxREF (high) */
        }
        channelMode(_base, ch, (uint8_t)(mode << 4 | OCPE));
        channelEnable(_base, ch, ccer);
    }
    CH32RV_TIM_SWEVGR(_base) = SWEVGR_COMG;                      /* all three at once */
    _step = n < 6u ? n : 0xFF;
    return true;
}

uint32_t CH32RVSixStep::setDeadTime(uint32_t nanoseconds)
{
    const uint32_t clock = ch32rv_timer_clock_hz(isAttached() ? _pads[0].timer : 1u);
    uint32_t ticks;
    _dtg = deadTimeField(clock, nanoseconds, ticks);
    if (isAttached()) {
        CH32RV_TIM_BDTR(_base) = (uint16_t)((CH32RV_TIM_BDTR(_base) & ~0xFFu) | _dtg);
    }
    return nanoseconds ? (uint32_t)(((uint64_t)ticks * 1000000000u + clock / 2u) / clock) : 0u;
}

void CH32RVSixStep::end()
{
    if (isAttached()) {
        ch32rv_timer_stop(&_lease);
        CH32RV_TIM_CCER(_base) = 0;
        CH32RV_TIM_BDTR(_base) = 0;
        TIM_CTLR2(_base) &= (uint16_t)~CTLR2_CCPC;
        for (uint8_t k = 0; k < 6; k++) {
            padLow(_pads[k].pin);
        }
    }
    _step = 0xFF;
    ch32rv_timer_release(&_lease);
}
