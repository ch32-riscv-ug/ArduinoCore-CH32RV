/* arduino-esp32's timer API (CH32RVHwTimer.h) on a whole TIM.
 *
 * The TIM counts freely over its whole width; its overflows (the update
 * interrupt) extend it to 64 bits - the raw count. The sketch's count is the
 * raw count plus an offset, which timerWrite() and an autoreload move, so the
 * timer itself is never stopped or reloaded and no tick is lost. The alarm
 * is channel 1 in compare mode on the alarm's low bits: its interrupt wakes
 * the service, which runs the callback once the raw count has reached the
 * alarm (a wake-up on an earlier turn of the counter is ignored). */
#include "Arduino.h"
#include "CH32RVHwTimer.h"
#include "CH32RVTimer.h"
#include "internal/ch32rv_irq.h"
#include "internal/ch32rv_registers.h"

#define CC1IF   (1u << 1)          /* INTFR / DMAINTENR bit of channel 1 */
#define CC1G    (1u << 1)          /* SWEVGR: the channel 1 event */

struct hw_timer_s {
    CH32RVTimerOwner owner;
    CH32RVTimerLease lease;
    uint32_t base;
    uint32_t frequency;
    uint8_t bits;                  /* the TIM's counter width */
    bool alarm_on;
    bool autoreload;
    uint64_t wraps;                /* overflows of the TIM's counter */
    uint64_t offset;               /* the sketch's count less the raw count */
    uint64_t alarm;                /* in raw counts */
    uint64_t period;               /* alarm - reload_count */
    void (*fn)(void *);
    void *arg;
};

static hw_timer_t hw_timers[CH32RV_TIMER_COUNT];

static bool alive(const hw_timer_t *t)
{
    return t && ch32rv_timer_lease_valid(&t->lease);
}

/* The raw count. With interrupts held (callers' job) an overflow can still
 * be pending: one flagged before a count in the lower half came first. */
static uint64_t raw_count(const hw_timer_t *t)
{
    uint32_t count;
    uint16_t pending, again;
    do {
        pending = CH32RV_TIM_INTFR(t->base) & CH32RV_TIM_INT_UIE;
        count = t->bits == 32u ? CH32RV_TIM_CNT32(t->base) : CH32RV_TIM_CNT(t->base);
        again = CH32RV_TIM_INTFR(t->base) & CH32RV_TIM_INT_UIE;
    } while (pending != again);
    const uint32_t half = t->bits == 32u ? 0x80000000u : 0x8000u;
    const uint64_t wraps = t->wraps + (pending && count < half ? 1u : 0u);
    return (wraps << t->bits) | count;
}

/* Channel 1's compare on the alarm's low bits; an alarm already due is
 * raised by hand. */
static void arm(hw_timer_t *t)
{
    if (!t->alarm_on) {
        CH32RV_TIM_DMAINTENR(t->base) &= (uint16_t)~CC1IF;
        return;
    }
    if (t->bits == 32u) {
        CH32RV_TIM_CHCVR32(t->base, 1) = (uint32_t)t->alarm;
    } else {
        CH32RV_TIM_CHCVR(t->base, 1) = (uint16_t)t->alarm;
    }
    CH32RV_TIM_DMAINTENR(t->base) |= CC1IF;
    if (raw_count(t) >= t->alarm) {
        CH32RV_TIM_SWEVGR(t->base) = CC1G;
    }
}

/* Both interrupts come here: count an overflow (unless the update vector's
 * dispatch took the flag, `wrapped`), then see whether the alarm is due. */
static void service(hw_timer_t *t, bool wrapped)
{
    if (!wrapped && (CH32RV_TIM_INTFR(t->base) & CH32RV_TIM_INT_UIE)) {
        CH32RV_TIM_INTFR(t->base) = (uint16_t)~CH32RV_TIM_INT_UIE;
        wrapped = true;
    }
    if (wrapped) {
        t->wraps++;
    }
    if ((CH32RV_TIM_INTFR(t->base) & CC1IF) == 0u) {
        return;
    }
    CH32RV_TIM_INTFR(t->base) = (uint16_t)~CC1IF;
    if (!t->alarm_on || raw_count(t) < t->alarm) {
        return;                    /* the compare on an earlier turn */
    }
    if (t->autoreload && t->period) {
        /* The count is reload_count at the alarm's tick, whenever this runs:
         * the offset drops by the period and the next alarm is a period on. */
        t->offset -= t->period;
        t->alarm += t->period;
    } else {
        t->alarm_on = false;
    }
    arm(t);
    if (t->fn) {
        t->fn(t->arg);
    }
}

static void on_update(void *self) { service((hw_timer_t *)self, true); }
static void on_channel(void *self) { service((hw_timer_t *)self, false); }

hw_timer_t *timerBegin(uint32_t frequency)
{
    hw_timer_t *t = 0;
    for (size_t i = 0; i < CH32RV_TIMER_COUNT; i++) {
        if (!hw_timers[i].owner.identity || !alive(&hw_timers[i])) {
            t = &hw_timers[i];
            break;
        }
    }
    if (!t || frequency == 0u) {
        return 0;
    }
    t->owner.identity = t;
    t->owner.quiesce = 0;
    t->owner.context = 0;
    /* The highest-numbered free timer: TIM1 and the low ones carry the
     * default PWM pads. */
    for (size_t i = CH32RV_TIMER_COUNT; i-- > 0;) {
        const CH32RVTimerCapability *cap = ch32rv_timer_capability(i);
        const uint32_t clock = ch32rv_timer_clock_hz(cap->number);
        if (cap->channels == 0u || frequency > clock) {
            continue;
        }
        const uint32_t prescale = (clock + frequency / 2u) / frequency;
        if (prescale > 65536u) {
            continue;
        }
        const CH32RVTimerRequest request = {
            cap->number, CH32RV_TIMER_WHOLE,
            {(uint16_t)(prescale - 1u), cap->counter_bits == 32u ? 0xFFFFFFFFu : 0xFFFFu,
             CH32RV_TIM_CTLR1_URS}
        };
        t->lease = ch32rv_timer_try_acquire(&request, &t->owner);
        if (!ch32rv_timer_lease_valid(&t->lease)) {
            continue;
        }
        ch32rv_timer_apply_base(&t->lease);
        t->base = cap->register_base;
        t->frequency = clock / prescale;
        t->bits = cap->counter_bits == 32u ? 32u : 16u;
        t->alarm_on = false;
        t->wraps = 0;
        t->offset = 0;
        t->fn = 0;
        /* Channel 1 a frozen compare: its flag, no output. */
        CH32RV_TIM_CHCTLR1(t->base) = 0;
        CH32RV_TIM_CCER(t->base) = 0;
        if (!ch32rv_timer_attach_update_interrupt(&t->lease, on_update, t) ||
            !ch32rv_timer_attach_channel_interrupt(&t->lease, on_channel, t)) {
            ch32rv_timer_release(&t->lease);
            return 0;
        }
        ch32rv_timer_start(&t->lease);
        return t;
    }
    t->owner.identity = 0;
    return 0;
}

void timerEnd(hw_timer_t *timer)
{
    if (!timer) {
        return;
    }
    ch32rv_timer_release(&timer->lease);       /* stops it, both interrupts too */
    timer->owner.identity = 0;
}

void timerStart(hw_timer_t *timer)
{
    if (alive(timer)) {
        ch32rv_timer_start(&timer->lease);
    }
}

void timerStop(hw_timer_t *timer)
{
    if (alive(timer)) {
        ch32rv_timer_stop(&timer->lease);
    }
}

void timerWrite(hw_timer_t *timer, uint64_t value)
{
    if (!alive(timer)) {
        return;
    }
    const uint32_t irq = ch32rv_irq_save();
    const uint64_t raw = raw_count(timer);
    /* The alarm stays at its count: in raw counts it moves with the offset. */
    const uint64_t logical_alarm = timer->alarm + timer->offset;
    timer->offset = value - raw;
    timer->alarm = logical_alarm - timer->offset;
    arm(timer);
    ch32rv_irq_restore(irq);
}

void timerRestart(hw_timer_t *timer)
{
    timerWrite(timer, 0);
}

uint64_t timerRead(hw_timer_t *timer)
{
    if (!alive(timer)) {
        return 0;
    }
    const uint32_t irq = ch32rv_irq_save();
    const uint64_t value = raw_count(timer) + timer->offset;
    ch32rv_irq_restore(irq);
    return value;
}

uint32_t timerGetFrequency(hw_timer_t *timer)
{
    return alive(timer) ? timer->frequency : 0u;
}

uint64_t timerReadMicros(hw_timer_t *timer)
{
    const uint32_t hz = timerGetFrequency(timer);
    return hz ? timerRead(timer) * 1000000u / hz : 0u;
}

uint64_t timerReadMillis(hw_timer_t *timer)
{
    const uint32_t hz = timerGetFrequency(timer);
    return hz ? timerRead(timer) * 1000u / hz : 0u;
}

double timerReadSeconds(hw_timer_t *timer)
{
    const uint32_t hz = timerGetFrequency(timer);
    return hz ? (double)timerRead(timer) / hz : 0.0;
}

void timerAttachInterruptArg(hw_timer_t *timer, void (*userFunc)(void *), void *arg)
{
    if (!alive(timer)) {
        return;
    }
    const uint32_t irq = ch32rv_irq_save();
    timer->fn = userFunc;
    timer->arg = arg;
    ch32rv_irq_restore(irq);
}

static void call_plain(void *fn)
{
    ((void (*)(void))fn)();
}

void timerAttachInterrupt(hw_timer_t *timer, void (*userFunc)(void))
{
    timerAttachInterruptArg(timer, userFunc ? call_plain : 0, (void *)userFunc);
}

void timerDetachInterrupt(hw_timer_t *timer)
{
    timerAttachInterruptArg(timer, 0, 0);
}

void timerAlarm(hw_timer_t *timer, uint64_t alarm_value, bool autoreload, uint64_t reload_count)
{
    if (!alive(timer)) {
        return;
    }
    const uint32_t irq = ch32rv_irq_save();
    timer->alarm = alarm_value - timer->offset;
    timer->autoreload = autoreload;
    timer->period = alarm_value > reload_count ? alarm_value - reload_count : 0u;
    timer->alarm_on = true;
    arm(timer);
    ch32rv_irq_restore(irq);
}
