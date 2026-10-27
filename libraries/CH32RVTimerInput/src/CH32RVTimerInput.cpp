#include "CH32RVTimerInput.h"

#include "ch32rv_gpio.h"
#include "ch32rv_irq.h"
#include "ch32rv_registers.h"

#ifndef CH32RV_PWM_PIN_COUNT
#define CH32RV_PWM_PIN_TO_TIMER(p)   ((void)(p), 0)
#define CH32RV_PWM_PIN_TO_CHANNEL(p) ((void)(p), 0)
#endif

namespace {

constexpr uint16_t UIF = CH32RV_TIM_INT_UIE;     /* same bit in INTFR */
constexpr uint16_t CKD_SHIFT = 8;                /* CTLR1 clock division */
constexpr uint16_t CKD_MASK = 3u << CKD_SHIFT;
constexpr uint16_t OVERCAPTURE = 3u << 9;        /* CC1OF, CC2OF */
/* CHCTLR1 input fields: CCxS (where ICx comes from) and ICxF (the filter). */
constexpr uint16_t CC1S_TI1 = 0x0001, CC1S_TI2 = 0x0002;
constexpr uint16_t CC2S_TI2 = 0x0100, CC2S_TI1 = 0x0200;
constexpr uint16_t ICF_MASK = 0xF0F0;
/* CCER: CCxE enables the capture, CCxP picks the falling edge. */
constexpr uint16_t CC1E = 0x0001, CC1P = 0x0002, CC2E = 0x0010, CC2P = 0x0020;
/* SMCFGR: TS (the trigger) and SMS (the slave mode). */
constexpr uint16_t TS_TI1FP1 = 5u << 4, TS_TI2FP2 = 6u << 4;
constexpr uint16_t SMS_ENCODER_TI2 = 1, SMS_ENCODER_TI1 = 2, SMS_ENCODER_BOTH = 3, SMS_RESET = 4;

/* The timer and channel (1 or 2) a pad reaches on its default route. */
bool timerPad(int pin, uint8_t &timer, uint8_t &channel)
{
    pin = CH32RV_PIN_RESOLVE(pin);
    timer = (uint8_t)CH32RV_PWM_PIN_TO_TIMER(pin);
    channel = (uint8_t)CH32RV_PWM_PIN_TO_CHANNEL(pin);
    return timer != 0 && (channel == 1 || channel == 2) && ch32TimerCapabilityFor(timer);
}

/* An input that the timer channel listens to. On the per-pin families the
 * pad also has to be switched to the timer's alternate function. */
void inputPin(int pin, uint8_t mode)
{
    pinMode(pin, mode);
#ifdef CH32RV_PWM_PIN_TO_AF
    pin = CH32RV_PIN_RESOLVE(pin);
    ch32rv_pin_set_af((uint8_t)pin, (uint32_t)CH32RV_PWM_PIN_TO_AF(pin));
#endif
}

/* The input filter closest to `cycles` without going over: ICxF settings 1-3
 * sample at F_CPU, 4-15 at F_CPU divided by CKD (1, 2 or 4). */
void filterFor(uint16_t cycles, uint8_t &icf, uint8_t &ckd)
{
    static const uint16_t length[16] = {0, 2, 4, 8, 12, 16, 24, 32, 48, 64, 80, 96, 128, 160, 192, 256};
    uint16_t best = 0;
    icf = 0;
    ckd = 0;
    for (uint8_t d = 0; d < 3; d++) {
        for (uint8_t f = 1; f < 16; f++) {
            const uint16_t n = f < 4 ? length[f] : (uint16_t)(length[f] << d);
            if (n <= cycles && n > best) {
                best = n;
                icf = f;
                ckd = d;
            }
        }
    }
}

void applyFilter(uint32_t base, uint8_t icf, uint8_t ckd)
{
    CH32RV_TIM_CHCTLR1(base) = (uint16_t)((CH32RV_TIM_CHCTLR1(base) & ~ICF_MASK) |
                                          (icf << 4) | (icf << 12));
    CH32RV_TIM_CTLR1(base) = (uint16_t)((CH32RV_TIM_CTLR1(base) & ~CKD_MASK) | (ckd << CKD_SHIFT));
}

void quiet(uint32_t base)
{
    CH32RV_TIM_SMCFGR(base) = 0;
    CH32RV_TIM_CCER(base) = 0;
    CH32RV_TIM_CHCTLR1(base) = 0;
}

}  // namespace

/* ---- CH32RVPulseCapture ---- */

bool CH32RVPulseCapture::begin(int pin, uint32_t minHz, uint8_t mode)
{
    end();
    uint8_t timer, channel;
    if (!timerPad(pin, timer, channel)) {
        return false;
    }
    const CH32RVTimerCapability *cap = ch32TimerCapabilityFor(timer);
    _wide = cap->counter_bits == 32u;
    if (minHz == 0u) {
        minHz = 1u;
    }
    /* The smallest prescaler that lets one period of minHz fit the counter. */
    const uint64_t span = (uint64_t)minHz << (_wide ? 32 : 16);
    uint64_t prescale = ((uint64_t)F_CPU + span - 1u) / span;
    if (prescale == 0u) {
        prescale = 1u;
    } else if (prescale > 65536u) {
        prescale = 65536u;
    }
    /* URS: only an overflow (no rising edge for a whole counter) raises the
     * update flag, not the reset each rising edge makes. */
    const CH32RVTimerRequest request = {
        timer, CH32RV_TIMER_WHOLE,
        {(uint16_t)(prescale - 1u), _wide ? 0xFFFFFFFFu : 0xFFFFu,
         (uint16_t)(CH32RV_TIM_CTLR1_URS | (_ckd << CKD_SHIFT))}
    };
    const CH32RVTimerOwner owner = {this, nullptr, nullptr};
    _lease = ch32TimerTryAcquire(&request, &owner);
    if (!ch32TimerLeaseValid(&_lease) || !ch32TimerApplyBase(&_lease)) {
        ch32TimerRelease(&_lease);
        return false;
    }
    inputPin(pin, mode);
    _base = cap->register_base;
    _tickHz = (uint32_t)(F_CPU / prescale);

    /* PWM-input mode: both captures listen to the pin's own input; the
     * rising edge captures the period and resets the counter (slave reset
     * mode), the falling edge captures the high time. */
    CH32RV_TIM_CCER(_base) = 0;
    if (channel == 1) {
        CH32RV_TIM_CHCTLR1(_base) = (uint16_t)(CC1S_TI1 | CC2S_TI1 | (_filter << 4) | (_filter << 12));
        CH32RV_TIM_CCER(_base) = (uint16_t)(CC1E | CC2E | CC2P);
        CH32RV_TIM_SMCFGR(_base) = (uint16_t)(TS_TI1FP1 | SMS_RESET);
        _periodCh = 1;
        _highCh = 2;
    } else {
        CH32RV_TIM_CHCTLR1(_base) = (uint16_t)(CC1S_TI2 | CC2S_TI2 | (_filter << 4) | (_filter << 12));
        CH32RV_TIM_CCER(_base) = (uint16_t)(CC1E | CC1P | CC2E);
        CH32RV_TIM_SMCFGR(_base) = (uint16_t)(TS_TI2FP2 | SMS_RESET);
        _periodCh = 2;
        _highCh = 1;
    }
    CH32RV_TIM_INTFR(_base) = 0;
    _overflowed = false;
    _stalled = false;
    _dropNext = true;            /* the counter did not start on a rising edge */
    ch32TimerStart(&_lease);
    return true;
}

void CH32RVPulseCapture::end()
{
    if (ch32TimerLeaseValid(&_lease)) {
        ch32TimerStop(&_lease);
        quiet(_base);
    }
    ch32TimerRelease(&_lease);
    _tickHz = 0;
}

void CH32RVPulseCapture::poll()
{
    if ((CH32RV_TIM_INTFR(_base) & UIF) != 0u) {
        CH32RV_TIM_INTFR(_base) = (uint16_t)~UIF;
        _overflowed = true;
        _stalled = true;
    }
}

bool CH32RVPulseCapture::read(Pulse &out)
{
    if (!ch32TimerLeaseValid(&_lease)) {
        return false;
    }
    poll();
    const uint16_t fresh = (uint16_t)(1u << _periodCh);
    if ((CH32RV_TIM_INTFR(_base) & fresh) == 0u) {
        return false;
    }
    /* Reading the period's capture clears its flag; the high time's flag and
     * the overcapture flags are cleared by hand (writing 0 clears). */
    uint32_t period, high;
    if (_wide) {
        period = CH32RV_TIM_CHCVR32(_base, _periodCh);
        high = CH32RV_TIM_CHCVR32(_base, _highCh);
    } else {
        period = CH32RV_TIM_CHCVR(_base, _periodCh);
        high = CH32RV_TIM_CHCVR(_base, _highCh);
    }
    CH32RV_TIM_INTFR(_base) = (uint16_t)~((1u << _highCh) | OVERCAPTURE);
    /* An overflow since the last capture means this period began before the
     * counter came round: drop it. Whether the overflow fell before or after
     * this capture is not known, so the next one is dropped as well. */
    const bool drop = _dropNext || _overflowed;
    _dropNext = _overflowed;
    _overflowed = false;
    if (drop) {
        return false;
    }
    /* The falling edge of the next period may already be in. */
    _stalled = false;
    out.period = period;
    out.high = high > period ? period : high;
    out.tickHz = _tickHz;
    return true;
}

bool CH32RVPulseCapture::stalled()
{
    if (!ch32TimerLeaseValid(&_lease)) {
        return false;
    }
    poll();
    /* Not cleared by a capture that follows: a signal a little slower than
     * minHz has rising edges between its overflows, and none of its periods
     * can be measured. Only a whole period read() returns ends the stall. */
    return _stalled;
}

void CH32RVPulseCapture::setFilter(uint16_t cycles)
{
    filterFor(cycles, _filter, _ckd);
    if (ch32TimerLeaseValid(&_lease)) {
        applyFilter(_base, _filter, _ckd);
    }
}

/* ---- CH32RVEncoder ---- */

bool CH32RVEncoder::attachFullQuad(int aPin, int bPin) { return attach(aPin, bPin, 4); }
bool CH32RVEncoder::attachHalfQuad(int aPin, int bPin) { return attach(aPin, bPin, 2); }
bool CH32RVEncoder::attachSingleEdge(int aPin, int bPin) { return attach(aPin, bPin, 1); }

bool CH32RVEncoder::attach(int aPin, int bPin, uint8_t edges)
{
    detach();
    uint8_t timer, channelA, timerB, channelB;
    if (!timerPad(aPin, timer, channelA) || !timerPad(bPin, timerB, channelB) ||
        timer != timerB || channelA == channelB) {
        return false;
    }
    const CH32RVTimerCapability *cap = ch32TimerCapabilityFor(timer);
    _wide = cap->counter_bits == 32u;
    const CH32RVTimerRequest request = {
        timer, CH32RV_TIMER_WHOLE,
        {0, _wide ? 0xFFFFFFFFu : 0xFFFFu, (uint16_t)(_ckd << CKD_SHIFT)}
    };
    const CH32RVTimerOwner owner = {this, nullptr, nullptr};
    _lease = ch32TimerTryAcquire(&request, &owner);
    if (!ch32TimerLeaseValid(&_lease) || !ch32TimerApplyBase(&_lease)) {
        ch32TimerRelease(&_lease);
        return false;
    }
    inputPin(aPin, _pull);
    inputPin(bPin, _pull);
    _base = cap->register_base;

    /* The timer counts up when TI1 leads TI2. With A on channel 2 that is B
     * leading, so the count is negated. Half quad counts A's edges: TI1's
     * (SMS 2) or, swapped, TI2's (SMS 1). */
    _swapped = channelA == 2;
    const uint16_t sms = edges == 4 ? SMS_ENCODER_BOTH : (_swapped ? SMS_ENCODER_TI2 : SMS_ENCODER_TI1);
    _shift = edges == 1 ? 1 : 0;
    CH32RV_TIM_CCER(_base) = 0;
    CH32RV_TIM_CHCTLR1(_base) = (uint16_t)(CC1S_TI1 | CC2S_TI2 | (_filter << 4) | (_filter << 12));
    CH32RV_TIM_CCER(_base) = (uint16_t)(CC1E | CC2E);
    CH32RV_TIM_SMCFGR(_base) = sms;
    if (_wide) {
        CH32RV_TIM_CNT32(_base) = 0;
    } else {
        CH32RV_TIM_CNT(_base) = 0;
    }
    _wraps = 0;
    _offset = 0;
    _last = 0;
    if (!ch32TimerAttachUpdateInterrupt(&_lease, onUpdate, this)) {
        quiet(_base);
        ch32TimerRelease(&_lease);
        return false;
    }
    ch32TimerStart(&_lease);
    return true;
}

void CH32RVEncoder::detach()
{
    if (ch32TimerLeaseValid(&_lease)) {
        _last = getCount();
        ch32TimerDetachUpdateInterrupt(&_lease);
        ch32TimerStop(&_lease);
        quiet(_base);
    }
    ch32TimerRelease(&_lease);
}

/* The counter came round (the core's dispatcher has cleared the flag). The
 * count is still next to the end it crossed: near 0 after counting up past
 * the top, near the top after counting down past 0. */
void CH32RVEncoder::onUpdate(void *self)
{
    CH32RVEncoder *e = static_cast<CH32RVEncoder *>(self);
    const uint32_t count = e->_wide ? CH32RV_TIM_CNT32(e->_base) : CH32RV_TIM_CNT(e->_base);
    const uint32_t half = e->_wide ? 0x80000000u : 0x8000u;
    e->_wraps = e->_wraps + (count < half ? 1 : -1);
}

int64_t CH32RVEncoder::raw()
{
    const uint32_t half = _wide ? 0x80000000u : 0x8000u;
    uint32_t count;
    uint16_t pending, again;
    int32_t wraps;
    /* With interrupts held the update can still arrive, as a pending flag:
     * read it on both sides of the count so that the two agree. */
    const uint32_t irq = ch32rv_irq_save();
    do {
        pending = CH32RV_TIM_INTFR(_base) & UIF;
        count = _wide ? CH32RV_TIM_CNT32(_base) : CH32RV_TIM_CNT(_base);
        again = CH32RV_TIM_INTFR(_base) & UIF;
    } while (pending != again);
    wraps = _wraps;
    ch32rv_irq_restore(irq);
    if (pending) {
        wraps += count < half ? 1 : -1;
    }
    const int64_t value = _wide ? (int64_t)wraps * 0x100000000LL + count
                                : (int64_t)wraps * 0x10000LL + count;
    return _swapped ? -value : value;
}

int64_t CH32RVEncoder::getCount()
{
    if (ch32TimerLeaseValid(&_lease)) {
        _last = (raw() >> _shift) + _offset;    /* >> on a negative count floors */
    }
    return _last;
}

void CH32RVEncoder::setCount(int64_t value)
{
    _last = value;
    if (ch32TimerLeaseValid(&_lease)) {
        _offset = value - (raw() >> _shift);
    } else {
        _offset = value;
    }
}

bool CH32RVEncoder::pauseCount()
{
    return ch32TimerStop(&_lease);
}

bool CH32RVEncoder::resumeCount()
{
    return ch32TimerStart(&_lease);
}

void CH32RVEncoder::setFilter(uint16_t cycles)
{
    filterFor(cycles, _filter, _ckd);
    if (ch32TimerLeaseValid(&_lease)) {
        applyFilter(_base, _filter, _ckd);
    }
}
