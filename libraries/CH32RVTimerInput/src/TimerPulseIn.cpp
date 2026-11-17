/* timerPulseIn(): pulseIn() by a timer's input capture (CH32RVTimerInput.h).
 *
 * The capture latches both edges in hardware: the pin's own channel the edge
 * that starts the pulse, its pair (1-2, 3-4) the edge that ends it, from the
 * same input. The width is right to the tick (1 us) however long the code
 * takes to see the flags; pulseIn()'s software loop moves with every
 * interrupt that lands inside the pulse. The timer is taken politely and
 * given back on return. */
#include "CH32RVTimerInput.h"

#include "internal/ch32rv_registers.h"

static const uint8_t pulse_owner_identity = 0;   /* its address is the owner's identity */
static const CH32RVTimerOwner pulse_owner = {&pulse_owner_identity, 0, 0};

typedef struct {
    uint32_t base;
    uint32_t wraps;          /* counter overflows seen */
    bool wide;
} PulseTimer;

/* Count an overflow the counter flagged. */
static void pulse_poll(PulseTimer *t)
{
    if (CH32RV_TIM_INTFR(t->base) & CH32RV_TIM_INT_UIE) {
        CH32RV_TIM_INTFR(t->base) = (uint16_t)~CH32RV_TIM_INT_UIE;
        t->wraps++;
    }
}

/* The time of channel ch's capture in ticks: the overflows before it and the
 * captured count. An overflow flagged but not counted yet came before a
 * capture in the counter's lower half. */
static uint32_t pulse_stamp(PulseTimer *t, uint8_t ch)
{
    const bool pending = (CH32RV_TIM_INTFR(t->base) & CH32RV_TIM_INT_UIE) != 0u;
    if (t->wide) {
        return CH32RV_TIM_CHCVR32(t->base, ch);   /* 71 minutes at 1 MHz */
    }
    const uint32_t count = CH32RV_TIM_CHCVR(t->base, ch);
    const uint32_t wraps = t->wraps + (pending && count < 0x8000u ? 1u : 0u);
    return (wraps << 16) + count;
}

/* Wait for channel ch's capture, counting overflows; false on the timeout. */
static bool pulse_wait(PulseTimer *t, uint8_t ch, unsigned long start, unsigned long timeout)
{
    const uint16_t flag = (uint16_t)(1u << ch);
    while ((CH32RV_TIM_INTFR(t->base) & flag) == 0u) {
        pulse_poll(t);
        if (micros() - start >= timeout) {
            return false;
        }
    }
    return true;
}

/* The pulse by capture on `pad`'s timer, which the caller holds whole. */
static unsigned long pulse_edges(PulseTimer *tp, const CH32RVTimerPad *pad, uint8_t state,
                                 unsigned long start, unsigned long timeout)
{
    PulseTimer t = *tp;
    const uint8_t own = pad->channel;
    const uint8_t pair = (uint8_t)(((own - 1u) ^ 1u) + 1u);
    const uint16_t start_edge = state ? 0u : 2u, end_edge = state ? 2u : 0u;
    const uint16_t ccer = (uint16_t)(((1u | start_edge) << ((own - 1u) * 4u)) |
                                     ((1u | end_edge) << ((pair - 1u) * 4u)));

    const PinStatus want = state ? HIGH : LOW;
    while (digitalRead(pad->pin) == want) {         /* finish a pulse in flight */
        if (micros() - start >= timeout) {
            return 0;
        }
    }
    CH32RV_TIM_CCER(t.base) = ccer;
    CH32RV_TIM_INTFR(t.base) = 0;
    CH32RV_TIM_CTLR1(t.base) |= CH32RV_TIM_CTLR1_CEN;
    if (!pulse_wait(&t, own, start, timeout)) {
        return 0;
    }
    uint32_t begin = pulse_stamp(&t, own);
    if (!pulse_wait(&t, pair, start, timeout)) {
        return 0;
    }
    const uint32_t end = pulse_stamp(&t, pair);
    /* More pulses than the loop saw (each shorter than one turn of it): the
     * start was captured again, and the end is the newest pulse's. */
    if (CH32RV_TIM_INTFR(t.base) & (1u << own)) {
        const uint32_t again = pulse_stamp(&t, own);
        if ((int32_t)(end - again) > 0) {
            begin = again;
        }
    }
    return end - begin;
}

static unsigned long pulse_capture(const CH32RVTimerCapability *cap, const CH32RVTimerPad *pad,
                                   uint8_t state, unsigned long start, unsigned long timeout)
{
    PulseTimer t = {cap->register_base, 0, cap->counter_bits == 32u};
    const uint8_t own = pad->channel;
    const uint8_t pair = (uint8_t)(((own - 1u) ^ 1u) + 1u);
    /* CCxS: 01 the channel's own input, 10 its pair's (both channels of a
     * pair sit in one CHCTLR). */
    volatile uint16_t *chctlr = own <= 2u ? &CH32RV_TIM_CHCTLR1(t.base) : &CH32RV_TIM_CHCTLR2(t.base);
    *chctlr = (uint16_t)((1u << (((own - 1u) & 1u) * 8u)) | (2u << (((pair - 1u) & 1u) * 8u)));
    const unsigned long width = pulse_edges(&t, pad, state, start, timeout);
    CH32RV_TIM_CCER(t.base) = 0;
    *chctlr = 0;
    return width;
}

unsigned long timerPulseIn(int pin, uint8_t state, unsigned long timeout)
{
    const unsigned long start = micros();
    CH32RVTimerPad pad;
    for (uint8_t n = 0; ch32rv_timer_pad_find((uint32_t)pin, CH32RV_TIMER_ROLE(CH32RV_TIMER_PAD_CH), n, &pad); n++) {
        const CH32RVTimerCapability *cap = ch32rv_timer_capability_for(pad.timer);
        const uint32_t clock = ch32rv_timer_clock_hz(pad.timer);
        /* A tick of 1 us: the width comes out in microseconds. */
        if (!cap || cap->channels < 4u || clock % 1000000u != 0u || clock / 1000000u > 65536u) {
            continue;
        }
        const CH32RVTimerRequest request = {
            pad.timer, CH32RV_TIMER_WHOLE,
            {(uint16_t)(clock / 1000000u - 1u), cap->counter_bits == 32u ? 0xFFFFFFFFu : 0xFFFFu, 0}
        };
        CH32RVTimerLease lease = ch32rv_timer_try_acquire(&request, &pulse_owner);
        if (!ch32rv_timer_lease_valid(&lease)) {
            continue;
        }
        ch32rv_timer_apply_base(&lease);
        ch32rv_timer_pad_connect(&pad, false);
        const unsigned long width = pulse_capture(cap, &pad, state, start, timeout);
        ch32rv_timer_release(&lease);            /* stops the timer, clears CCER */
        return width;
    }
    return pulseIn(pin, state, timeout);       /* no timer for it: the software loop */
}
