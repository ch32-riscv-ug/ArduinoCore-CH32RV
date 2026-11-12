/* The TIM update interrupt: who has it on each timer, the dispatch, and the
 * vectors. A file of its own, linked only when something attaches an update
 * interrupt (tone(), Servo, CH32RVTimerInput's encoder): the vectors would
 * otherwise come with every analogWrite(). Its forget / owned replace
 * ch32rv_timer.c's weak ones. */
#include "Arduino.h"
#include "CH32RVTimer.h"
#include "internal/ch32rv_registers.h"
#include "internal/ch32rv_timer_internal.h"
#include "internal/ch32rv_interrupt.h"

typedef struct {
    CH32RVTimerCallback callback;
    void *context;
    const void *owner;
} Update;

static Update updates[CH32RV_TIMER_COUNT];

void ch32rv_timer_update_forget(size_t index, const void *identity)
{
    if (!identity || updates[index].owner == identity) {
        updates[index].callback = 0;
        updates[index].context = 0;
        updates[index].owner = 0;
    }
}

bool ch32rv_timer_update_owned(size_t index)
{
    return updates[index].owner != 0;
}

bool ch32rv_timer_attach_update_interrupt(const CH32RVTimerLease *lease,
                                    CH32RVTimerCallback callback, void *context)
{
    if (!callback || !ch32rv_timer_lease_valid(lease) ||
        lease->channel != CH32RV_TIMER_WHOLE) {
        return false;
    }
    int index = ch32rv_timer_index(lease->timer);
    uint32_t irq_state = ch32rv_irq_save();
    Update *state = &updates[index];
    if (state->owner && state->owner != lease->owner_identity) {
        ch32rv_irq_restore(irq_state);
        return false;
    }
    state->callback = callback;
    state->context = context;
    state->owner = lease->owner_identity;
    const CH32RVTimerCapability *cap = &ch32rv_timer_caps[index];
    /* Do not deliver an update event left pending before this owner attached. */
    CH32RV_TIM_INTFR(cap->register_base) = (uint16_t)~CH32RV_TIM_INT_UIE;
    CH32RV_TIM_DMAINTENR(cap->register_base) |= CH32RV_TIM_INT_UIE;
    ch32rv_irq_enable(cap->update_irq);
    ch32rv_irq_restore(irq_state);
    return true;
}

void ch32rv_timer_detach_update_interrupt(const CH32RVTimerLease *lease)
{
    if (!ch32rv_timer_lease_valid(lease)) {
        return;
    }
    int index = ch32rv_timer_index(lease->timer);
    uint32_t irq_state = ch32rv_irq_save();
    Update *state = &updates[index];
    if (state->owner == lease->owner_identity) {
        const CH32RVTimerCapability *cap = &ch32rv_timer_caps[index];
        CH32RV_TIM_DMAINTENR(cap->register_base) &= (uint16_t)~CH32RV_TIM_INT_UIE;
        ch32rv_irq_disable(cap->update_irq);
        state->callback = 0;
        state->context = 0;
        state->owner = 0;
    }
    ch32rv_irq_restore(irq_state);
}

static void timer_irq_dispatch(uint8_t timer)
{
    int index = ch32rv_timer_index(timer);
    if (index < 0) {
        return;
    }
    const CH32RVTimerCapability *cap = &ch32rv_timer_caps[index];
    if ((CH32RV_TIM_INTFR(cap->register_base) & CH32RV_TIM_INT_UIE) == 0u) {
        return;
    }
    CH32RV_TIM_INTFR(cap->register_base) = (uint16_t)~CH32RV_TIM_INT_UIE;
    CH32RVTimerCallback callback = updates[index].callback;
    if (callback) {
        callback(updates[index].context);
    }
}

#define TIMER_HANDLER(number, kind, bits, channels, complementary, base,       \
                      clkaddr, clkmask, irq, handler)                           \
    CH32RV_INTERRUPT void handler(void)                               \
    {                                                                            \
        timer_irq_dispatch(number);                                               \
    }
CH32RV_TIMER_TABLE(TIMER_HANDLER)
#undef TIMER_HANDLER
