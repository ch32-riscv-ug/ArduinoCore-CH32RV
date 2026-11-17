#include "CH32RVTimerInput.h"

#include "internal/ch32rv_gpio.h"
#include "internal/ch32rv_irq.h"
#include "internal/ch32rv_registers.h"

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

/* The n-th way `pin` reaches channel 1 or 2 of a timer, on any route. */
bool channelPad(int pin, uint8_t n, CH32RVTimerPad &pad)
{
    uint8_t seen = 0;
    for (uint8_t i = 0; ch32rv_timer_pad_find((uint32_t)pin, CH32RV_TIMER_ROLE(CH32RV_TIMER_PAD_CH), i, &pad); i++) {
        if ((pad.channel == 1 || pad.channel == 2) && ch32rv_timer_capability_for(pad.timer) && seen++ == n) {
            return true;
        }
    }
    return false;
}

/* An input that the timer channel listens to: the pad's mode, then its
 * route (the timer's remap, or the pad's alternate function). */
void inputPin(const CH32RVTimerPad &pad, uint8_t mode)
{
    pinMode(pad.pin, mode);
    ch32rv_timer_pad_connect(&pad, false);
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
    CH32RV_TIM_CHCTLR2(base) = 0;
}

/* Channel ch's half of its CHCTLR (channels 1-2 share one, 3-4 the other;
 * the even channel in the high byte): CCxS (where ICx comes from: 1 its own
 * input, 2 its pair's) and ICxF (the filter). */
void captureFrom(uint32_t base, uint8_t ch, uint8_t source, uint8_t icf)
{
    volatile uint16_t &reg = ch <= 2 ? CH32RV_TIM_CHCTLR1(base) : CH32RV_TIM_CHCTLR2(base);
    const unsigned shift = ((ch - 1u) & 1u) * 8u;
    reg = (uint16_t)((reg & ~(0xFFu << shift)) | ((source | (icf << 4)) << shift));
}

uint32_t captured(uint32_t base, uint8_t ch, bool wide)
{
    return wide ? CH32RV_TIM_CHCVR32(base, ch) : CH32RV_TIM_CHCVR(base, ch);
}

}  // namespace

/* ---- CH32RVPulseCapture ---- */

bool CH32RVPulseCapture::begin(int pin, uint32_t minHz, uint8_t mode)
{
    end();
    if (minHz == 0u) {
        minHz = 1u;
    }
    CH32RVTimerPad pad;
    for (uint8_t n = 0; channelPad(pin, n, pad); n++) {
        if (begin(pad, minHz, mode)) {
            return true;
        }
    }
    return false;
}

bool CH32RVPulseCapture::begin(const CH32RVTimerPad &pad, uint32_t minHz, uint8_t mode)
{
    const uint8_t timer = pad.timer, channel = pad.channel;
    const CH32RVTimerCapability *cap = ch32rv_timer_capability_for(timer);
    _wide = cap->counter_bits == 32u;
    /* The smallest prescaler that lets one period of minHz fit the counter. */
    const uint64_t span = (uint64_t)minHz << (_wide ? 32 : 16);
    const uint32_t clock = ch32rv_timer_clock_hz(timer);
    uint64_t prescale = ((uint64_t)clock + span - 1u) / span;
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
    _lease = ch32rv_timer_try_acquire(&request, &_owner);
    if (!ch32rv_timer_lease_valid(&_lease) || !ch32rv_timer_apply_base(&_lease)) {
        ch32rv_timer_release(&_lease);
        return false;
    }
    inputPin(pad, mode);
    _base = cap->register_base;
    _tickHz = (uint32_t)(clock / prescale);

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
    ch32rv_timer_start(&_lease);
    return true;
}

void CH32RVPulseCapture::end()
{
    if (ch32rv_timer_lease_valid(&_lease)) {
        ch32rv_timer_stop(&_lease);
        quiet(_base);
    }
    ch32rv_timer_release(&_lease);
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
    if (!ch32rv_timer_lease_valid(&_lease)) {
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
    if (!ch32rv_timer_lease_valid(&_lease)) {
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
    if (ch32rv_timer_lease_valid(&_lease)) {
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
    CH32RVTimerPad a, b;
    for (uint8_t i = 0; channelPad(aPin, i, a); i++) {
        for (uint8_t j = 0; channelPad(bPin, j, b); j++) {
            if (a.channel != b.channel && ch32rv_timer_pads_together(&a, &b) && attach(a, b, edges)) {
                return true;
            }
        }
    }
    return false;
}

bool CH32RVEncoder::attach(const CH32RVTimerPad &a, const CH32RVTimerPad &b, uint8_t edges)
{
    const uint8_t timer = a.timer, channelA = a.channel;
    const CH32RVTimerCapability *cap = ch32rv_timer_capability_for(timer);
    _wide = cap->counter_bits == 32u;
    const CH32RVTimerRequest request = {
        timer, CH32RV_TIMER_WHOLE,
        {0, _wide ? 0xFFFFFFFFu : 0xFFFFu, (uint16_t)(_ckd << CKD_SHIFT)}
    };
    _lease = ch32rv_timer_try_acquire(&request, &_owner);
    if (!ch32rv_timer_lease_valid(&_lease) || !ch32rv_timer_apply_base(&_lease)) {
        ch32rv_timer_release(&_lease);
        return false;
    }
    inputPin(a, _pull);
    inputPin(b, _pull);
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
    if (!ch32rv_timer_attach_update_interrupt(&_lease, onUpdate, this)) {
        quiet(_base);
        ch32rv_timer_release(&_lease);
        return false;
    }
    ch32rv_timer_start(&_lease);
    return true;
}

void CH32RVEncoder::detach()
{
    if (ch32rv_timer_lease_valid(&_lease)) {
        _last = getCount();
        ch32rv_timer_detach_update_interrupt(&_lease);
        ch32rv_timer_stop(&_lease);
        quiet(_base);
    }
    ch32rv_timer_release(&_lease);
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
    if (ch32rv_timer_lease_valid(&_lease)) {
        _last = (raw() >> _shift) + _offset;    /* >> on a negative count floors */
    }
    return _last;
}

void CH32RVEncoder::setCount(int64_t value)
{
    _last = value;
    if (ch32rv_timer_lease_valid(&_lease)) {
        _offset = value - (raw() >> _shift);
    } else {
        _offset = value;
    }
}

bool CH32RVEncoder::pauseCount()
{
    return ch32rv_timer_stop(&_lease);
}

bool CH32RVEncoder::resumeCount()
{
    return ch32rv_timer_start(&_lease);
}

void CH32RVEncoder::setFilter(uint16_t cycles)
{
    filterFor(cycles, _filter, _ckd);
    if (ch32rv_timer_lease_valid(&_lease)) {
        applyFilter(_base, _filter, _ckd);
    }
}

/* ---- CH32RVInputCapture ---- */

bool CH32RVInputCapture::begin(int pin, int edge, uint32_t tickHz, uint8_t mode)
{
    end();
    CH32RVTimerPad pad;
    for (uint8_t n = 0; ch32rv_timer_pad_find((uint32_t)pin, CH32RV_TIMER_ROLE(CH32RV_TIMER_PAD_CH), n, &pad); n++) {
        if (begin(pad, edge, tickHz, mode)) {
            return true;
        }
    }
    return false;
}

bool CH32RVInputCapture::begin(const CH32RVTimerPad &pad, int edge, uint32_t tickHz, uint8_t mode)
{
    const CH32RVTimerCapability *cap = ch32rv_timer_capability_for(pad.timer);
    const uint8_t pair = (uint8_t)(((pad.channel - 1u) ^ 1u) + 1u);
    const uint32_t clock = ch32rv_timer_clock_hz(pad.timer);
    if (!cap || tickHz == 0u || tickHz > clock || (edge == CHANGE && pair > cap->channels)) {
        return false;
    }
    const uint32_t prescale = (clock + tickHz / 2u) / tickHz;
    if (prescale > 65536u) {
        return false;
    }
    _wide = cap->counter_bits == 32u;
    const CH32RVTimerRequest request = {
        pad.timer, CH32RV_TIMER_WHOLE,
        {(uint16_t)(prescale - 1u), _wide ? 0xFFFFFFFFu : 0xFFFFu,
         (uint16_t)(CH32RV_TIM_CTLR1_URS | (_ckd << CKD_SHIFT))}
    };
    _lease = ch32rv_timer_try_acquire(&request, &_owner);
    if (!ch32rv_timer_lease_valid(&_lease) || !ch32rv_timer_apply_base(&_lease)) {
        ch32rv_timer_release(&_lease);
        return false;
    }
    inputPin(pad, mode);
    _base = cap->register_base;
    _tickHz = clock / prescale;
    _own = pad.channel;
    _pair = edge == CHANGE ? pair : 0;
    _ownRising = edge != FALLING;
    _wraps = 0;
    _head = _tail = 0;
    _lost = 0;

    /* The channel latches its own input on the edge asked for (rising for
     * CHANGE); for CHANGE its pair latches the same input's falling edges. */
    quiet(_base);
    captureFrom(_base, _own, 1, _filter);
    uint16_t ccer = (uint16_t)((_ownRising ? 1u : 3u) << ((_own - 1u) * 4u));
    uint16_t enable = (uint16_t)(1u << _own);
    if (_pair) {
        captureFrom(_base, _pair, 2, _filter);
        ccer |= (uint16_t)(3u << ((_pair - 1u) * 4u));
        enable |= (uint16_t)(1u << _pair);
    }
    CH32RV_TIM_INTFR(_base) = 0;
    CH32RV_TIM_DMAINTENR(_base) |= enable;
    if (!ch32rv_timer_attach_update_interrupt(&_lease, onUpdate, this) ||
        !ch32rv_timer_attach_channel_interrupt(&_lease, onInterrupt, this)) {
        quiet(_base);
        ch32rv_timer_release(&_lease);
        return false;
    }
    /* The counter first, the captures after: an edge latched while it stood
     * still had the count of before it started (X035's first falling edge
     * came 23 us early, 2026-11-17 bench). */
    ch32rv_timer_start(&_lease);
    CH32RV_TIM_CCER(_base) = ccer;
    return true;
}

void CH32RVInputCapture::end()
{
    if (ch32rv_timer_lease_valid(&_lease)) {
        ch32rv_timer_detach_channel_interrupt(&_lease);
        ch32rv_timer_detach_update_interrupt(&_lease);
        ch32rv_timer_stop(&_lease);
        quiet(_base);
    }
    ch32rv_timer_release(&_lease);
    _tickHz = 0;
}

void CH32RVInputCapture::onInterrupt(void *self) { static_cast<CH32RVInputCapture *>(self)->service(false); }
void CH32RVInputCapture::onUpdate(void *self) { static_cast<CH32RVInputCapture *>(self)->service(true); }

/* The captures first, then the overflow: an overflow flagged by the time
 * the captures are read came after any capture in the counter's upper half
 * and before any in its lower half (the interrupt runs well within half a
 * turn of the counter). `wrapped`: the update vector's dispatch took the
 * overflow's flag already. */
void CH32RVInputCapture::service(bool wrapped)
{
    const uint16_t flags = CH32RV_TIM_INTFR(_base);
    const bool ownIn = (flags & (1u << _own)) != 0u;
    const bool pairIn = _pair && (flags & (1u << _pair)) != 0u;
    const uint32_t own = ownIn ? captured(_base, _own, _wide) : 0u;
    const uint32_t pair = pairIn ? captured(_base, _pair, _wide) : 0u;
    /* Cleared by hand as well: X035's TIM3 kept its capture flag through the
     * read and the interrupt never ended (2026-11-17 bench). */
    const uint16_t taken = (uint16_t)((ownIn ? 1u << _own : 0u) | (pairIn ? 1u << _pair : 0u));
    if (taken) {
        CH32RV_TIM_INTFR(_base) = (uint16_t)~taken;
    }
    const uint16_t over = (uint16_t)(flags & ((1u << (_own + 8u)) | (_pair ? 1u << (_pair + 8u) : 0u)));
    if (over) {
        CH32RV_TIM_INTFR(_base) = (uint16_t)~over;
        _lost = _lost + 1u;
    }
    if (!wrapped && (CH32RV_TIM_INTFR(_base) & CH32RV_TIM_INT_UIE)) {
        CH32RV_TIM_INTFR(_base) = (uint16_t)~CH32RV_TIM_INT_UIE;
        wrapped = true;
    }
    const uint32_t half = _wide ? 0x80000000u : 0x8000u;
    const unsigned bits = _wide ? 32u : 16u;
    Edge a = {((_wraps + (wrapped && own < half ? 1u : 0u)) << bits) | own, _ownRising};
    Edge b = {((_wraps + (wrapped && pair < half ? 1u : 0u)) << bits) | pair, false};
    if (wrapped) {
        _wraps = _wraps + 1u;
    }
    if (ownIn && pairIn && b.ticks < a.ticks) {
        deliver(b);
        deliver(a);
    } else {
        if (ownIn) {
            deliver(a);
        }
        if (pairIn) {
            deliver(b);
        }
    }
}

void CH32RVInputCapture::deliver(const Edge &e)
{
    if (_callback) {
        _callback(e);
        return;
    }
    const uint8_t next = (uint8_t)((_head + 1u) % (QUEUE + 1u));
    if (next == _tail) {
        _lost = _lost + 1u;
        return;
    }
    _queue[_head] = e;
    _head = next;
}

void CH32RVInputCapture::onEdge(Callback callback)
{
    const uint32_t irq = ch32rv_irq_save();
    _callback = callback;
    ch32rv_irq_restore(irq);
}

int CH32RVInputCapture::available()
{
    return (int)((_head + QUEUE + 1u - _tail) % (QUEUE + 1u));
}

bool CH32RVInputCapture::read(Edge &out)
{
    const uint32_t irq = ch32rv_irq_save();
    const bool any = _head != _tail;
    if (any) {
        out = _queue[_tail];
        _tail = (uint8_t)((_tail + 1u) % (QUEUE + 1u));
    }
    ch32rv_irq_restore(irq);
    return any;
}

uint64_t CH32RVInputCapture::now()
{
    if (!ch32rv_timer_lease_valid(&_lease)) {
        return 0;
    }
    const uint32_t irq = ch32rv_irq_save();
    uint32_t count;
    uint16_t pending, again;
    do {
        pending = CH32RV_TIM_INTFR(_base) & CH32RV_TIM_INT_UIE;
        count = _wide ? CH32RV_TIM_CNT32(_base) : CH32RV_TIM_CNT(_base);
        again = CH32RV_TIM_INTFR(_base) & CH32RV_TIM_INT_UIE;
    } while (pending != again);
    const uint32_t half = _wide ? 0x80000000u : 0x8000u;
    const uint64_t wraps = _wraps + (pending && count < half ? 1u : 0u);
    ch32rv_irq_restore(irq);
    return (wraps << (_wide ? 32u : 16u)) | count;
}

void CH32RVInputCapture::setFilter(uint16_t cycles)
{
    filterFor(cycles, _filter, _ckd);
}
