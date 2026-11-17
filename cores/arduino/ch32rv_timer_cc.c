/* The TIM capture / compare interrupt: who has it on each timer, the
 * dispatch, and the advanced timers' own capture / compare vectors. Linked
 * only by a sketch that attaches one (input capture, output compare, the
 * user timer). The general timers share one vector between update and
 * channels: that vector is ch32rv_timer_irq.c's, which calls the dispatch
 * here. */
#include "Arduino.h"
#include "CH32RVTimer.h"
#include "internal/ch32rv_registers.h"
#include "internal/ch32rv_timer_internal.h"
#include "internal/ch32rv_interrupt.h"

typedef struct {
    CH32RVTimerCallback callback;
    void *context;
    const void *owner;
} Channel;

static Channel channels[CH32RV_TIMER_COUNT];

/* The capture / compare IRQ of a timer whose channels have a vector apart
 * from the update's, 0 where they share it. */
static uint32_t cc_irq(uint8_t timer)
{
#ifdef CH32RV_TIMER_CC_TABLE
#define CC_IRQ(number, irq, handler) \
    if (timer == (number)) {         \
        return (irq);                \
    }
    CH32RV_TIMER_CC_TABLE(CC_IRQ)
#undef CC_IRQ
#endif
    (void)timer;
    return 0;
}

static void disable(size_t index)
{
    const CH32RVTimerCapability *cap = &ch32rv_timer_caps[index];
    const uint32_t irq = cc_irq(cap->number);
    /* CCxIE, CCxDE and the trigger's: the update's (bit 0) stays. */
    CH32RV_TIM_DMAINTENR(cap->register_base) &= CH32RV_TIM_INT_UIE;
    if (irq) {
        ch32rv_irq_disable(irq);
    } else if (!ch32rv_timer_update_owned(index)) {
        ch32rv_irq_disable(cap->update_irq);
    }
}

void ch32rv_timer_channel_forget(size_t index, const void *identity)
{
    if (!identity || channels[index].owner == identity) {
        if (channels[index].owner) {
            disable(index);
        }
        channels[index].callback = 0;
        channels[index].context = 0;
        channels[index].owner = 0;
    }
}

void ch32rv_timer_channel_dispatch(size_t index)
{
    CH32RVTimerCallback callback = channels[index].callback;
    if (callback) {
        callback(channels[index].context);
    }
}

bool ch32rv_timer_attach_channel_interrupt(const CH32RVTimerLease *lease,
                                           CH32RVTimerCallback callback,
                                           void *context)
{
    if (!callback || !ch32rv_timer_lease_valid(lease) ||
        lease->channel != CH32RV_TIMER_WHOLE) {
        return false;
    }
    const int index = ch32rv_timer_index(lease->timer);
    ch32rv_timer_irq_link();     /* the shared vectors are that file's */
    const uint32_t irq_state = ch32rv_irq_save();
    Channel *state = &channels[index];
    if (state->owner && state->owner != lease->owner_identity) {
        ch32rv_irq_restore(irq_state);
        return false;
    }
    state->callback = callback;
    state->context = context;
    state->owner = lease->owner_identity;
    const CH32RVTimerCapability *cap = &ch32rv_timer_caps[index];
    const uint32_t irq = cc_irq(cap->number);
    ch32rv_irq_enable(irq ? irq : cap->update_irq);
    ch32rv_irq_restore(irq_state);
    return true;
}

void ch32rv_timer_detach_channel_interrupt(const CH32RVTimerLease *lease)
{
    if (!ch32rv_timer_lease_valid(lease)) {
        return;
    }
    const int index = ch32rv_timer_index(lease->timer);
    const uint32_t irq_state = ch32rv_irq_save();
    ch32rv_timer_channel_forget((size_t)index, lease->owner_identity);
    ch32rv_irq_restore(irq_state);
}

#ifdef CH32RV_TIMER_CC_TABLE
static void cc_dispatch(uint8_t timer)
{
    const int index = ch32rv_timer_index(timer);
    if (index >= 0) {
        ch32rv_timer_channel_dispatch((size_t)index);
    }
}

#define CC_HANDLER(number, irq, handler) \
    CH32RV_INTERRUPT void handler(void)  \
    {                                    \
        cc_dispatch(number);             \
    }
CH32RV_TIMER_CC_TABLE(CC_HANDLER)
#undef CC_HANDLER
#endif
