/* Shared TIM ownership. The update interrupt (attach, dispatch, the vectors)
 * is ch32rv_timer_irq.c's, linked only by a sketch that attaches one: PWM
 * alone carries none of it.
 *
 * There is no heap and no hidden lock policy.  Arduino's void standard APIs
 * use takeover; libraries that can report a busy resource use tryAcquire.
 */
#include "Arduino.h"
#include "CH32RVTimer.h"
#include "internal/ch32rv_bus_clock.h"
#include "internal/ch32rv_registers.h"
#include "internal/ch32rv_timer_internal.h"

/* Owners are kept by pointer: the owner object outlives its lease (the
 * header's contract), and a pointer is a quarter of a copy - on V003, two
 * timers' states went from 216 B to under 100. */
typedef struct {
    const CH32RVTimerOwner *owner;
    uint16_t generation;
} ChannelOwner;

typedef struct {
    CH32RVTimerBaseConfig base;
    const CH32RVTimerOwner *whole_owner;
    ChannelOwner channel[4];
    uint16_t base_generation;
    uint16_t applied_generation;
    bool configured;
} TimerState;

#define TIMER_CAP(number, kind, bits, channels, complementary, base, clkaddr, \
                  clkmask, irq, handler)                                      \
    { (number), (kind), (bits), (channels), (complementary) != 0, (base),      \
      (clkaddr), (clkmask), (irq) },
const CH32RVTimerCapability ch32rv_timer_caps[] = {
    CH32RV_TIMER_TABLE(TIMER_CAP)
};
#undef TIMER_CAP

static TimerState timer_states[CH32RV_TIMER_COUNT];

static uint32_t irq_save(void) { return ch32rv_irq_save(); }

static void irq_restore(uint32_t old) { ch32rv_irq_restore(old); }

/* What ch32rv_timer_irq.c keeps of the update interrupt; without that file
 * there is none to forget or report. */
__attribute__((weak)) void ch32rv_timer_update_forget(size_t index, const void *identity)
{
    (void)index;
    (void)identity;
}

__attribute__((weak)) bool ch32rv_timer_update_owned(size_t index)
{
    (void)index;
    return false;
}

static uint16_t next_generation(uint16_t value)
{
    value++;
    return value ? value : 1u;
}

int ch32rv_timer_index(uint8_t timer)
{
    for (size_t i = 0; i < CH32RV_TIMER_COUNT; i++) {
        if (ch32rv_timer_caps[i].number == timer) {
            return (int)i;
        }
    }
    return -1;
}

static bool owner_present(const CH32RVTimerOwner *owner)
{
    return owner && owner->identity;
}

static bool same_base(const CH32RVTimerBaseConfig *a,
                      const CH32RVTimerBaseConfig *b)
{
    return a->prescaler == b->prescaler && a->reload == b->reload &&
           a->control == b->control;
}

static bool any_channel_owner(const TimerState *state)
{
    for (unsigned i = 0; i < 4; i++) {
        if (state->channel[i].owner) {
            return true;
        }
    }
    return false;
}

static void stop_hardware(const CH32RVTimerCapability *cap)
{
    CH32RV_TIM_CTLR1(cap->register_base) = 0;
    /* An encoder or capture owner leaves a slave mode behind; the next owner's
     * counter would still follow its pins. */
    CH32RV_TIM_SMCFGR(cap->register_base) = 0;
    CH32RV_TIM_DMAINTENR(cap->register_base) = 0;
    CH32RV_TIM_CCER(cap->register_base) = 0;
    CH32RV_TIM_INTFR(cap->register_base) = 0;
    ch32rv_irq_disable(cap->update_irq);
}

static void disable_channel(const CH32RVTimerCapability *cap, uint8_t channel)
{
    const uint16_t enable = (uint16_t)(1u << ((channel - 1u) * 4u));
    const uint16_t interrupt = (uint16_t)(1u << channel);
    CH32RV_TIM_CCER(cap->register_base) &= (uint16_t)~enable;
    CH32RV_TIM_DMAINTENR(cap->register_base) &= (uint16_t)~interrupt;
    CH32RV_TIM_INTFR(cap->register_base) = (uint16_t)~interrupt;
}

static void call_quiesce(const CH32RVTimerOwner *owner, uint8_t timer,
                         uint8_t channel)
{
    if (owner && owner->quiesce) {
        owner->quiesce(owner->context, timer, channel);
    }
}

static void invalidate_channel(size_t index, uint8_t channel)
{
    TimerState *state = &timer_states[index];
    const CH32RVTimerCapability *cap = &ch32rv_timer_caps[index];
    const CH32RVTimerOwner *old = state->channel[channel - 1u].owner;

    disable_channel(cap, channel);
    state->channel[channel - 1u].owner = 0;
    state->channel[channel - 1u].generation =
        next_generation(state->channel[channel - 1u].generation);
    if (old) {
        ch32rv_timer_update_forget(index, old->identity);
    }
    call_quiesce(old, cap->number, channel);
}

static void invalidate_all(size_t index)
{
    TimerState *state = &timer_states[index];
    const CH32RVTimerCapability *cap = &ch32rv_timer_caps[index];
    const CH32RVTimerOwner *owners[5];
    unsigned count = 0;

    stop_hardware(cap);
    if (state->whole_owner) {
        owners[count] = state->whole_owner;
        count++;
    }
    for (unsigned i = 0; i < 4; i++) {
        const CH32RVTimerOwner *owner = state->channel[i].owner;
        if (!owner) {
            continue;
        }
        bool duplicate = false;
        for (unsigned j = 0; j < count; j++) {
            if (owners[j]->identity == owner->identity) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            owners[count] = owner;
            count++;
        }
    }

    state->whole_owner = 0;
    for (unsigned i = 0; i < 4; i++) {
        state->channel[i].owner = 0;
        state->channel[i].generation =
            next_generation(state->channel[i].generation);
    }
    ch32rv_timer_update_forget(index, 0);
    state->configured = false;
    state->base_generation = next_generation(state->base_generation);
    state->applied_generation = 0;

    for (unsigned i = 0; i < count; i++) {
        call_quiesce(owners[i], cap->number, CH32RV_TIMER_WHOLE);
    }
}

static CH32RVTimerLease invalid_lease(void)
{
    CH32RVTimerLease lease = {0};
    return lease;
}

static CH32RVTimerLease acquire_at(size_t index, const CH32RVTimerRequest *request,
                                 const CH32RVTimerOwner *owner, bool takeover)
{
    const CH32RVTimerCapability *cap = &ch32rv_timer_caps[index];
    TimerState *state = &timer_states[index];
    const uint8_t channel = request->channel;

    if (channel > cap->channels || (channel == CH32RV_TIMER_WHOLE &&
                                    cap->update_irq == 0u)) {
        return invalid_lease();
    }

    const bool occupied = state->whole_owner || any_channel_owner(state);
    const bool base_compatible = !state->configured ||
                                 same_base(&state->base, &request->base);

    if (channel == CH32RV_TIMER_WHOLE) {
        if (occupied && state->whole_owner &&
            state->whole_owner->identity == owner->identity &&
            !any_channel_owner(state) && base_compatible) {
            state->whole_owner = owner;
        } else if (occupied) {
            if (!takeover) {
                return invalid_lease();
            }
            invalidate_all(index);
        }
        if (!state->whole_owner) {
            state->base = request->base;
            state->configured = true;
            state->base_generation = next_generation(state->base_generation);
            state->applied_generation = 0;
            state->whole_owner = owner;
        }
        return (CH32RVTimerLease){cap->number, CH32RV_TIMER_WHOLE,
                                state->base_generation, 0, owner->identity};
    }

    if (state->whole_owner || (occupied && !base_compatible)) {
        if (!takeover) {
            return invalid_lease();
        }
        invalidate_all(index);
    }

    ChannelOwner *slot = &state->channel[channel - 1u];
    if (slot->owner && slot->owner->identity != owner->identity) {
        if (!takeover) {
            return invalid_lease();
        }
        invalidate_channel(index, channel);
    }

    if (!state->configured) {
        state->base = request->base;
        state->configured = true;
        state->base_generation = next_generation(state->base_generation);
        state->applied_generation = 0;
    }
    if (!slot->owner) {
        slot->generation = next_generation(slot->generation);
    }
    slot->owner = owner;
    return (CH32RVTimerLease){cap->number, channel, state->base_generation,
                            slot->generation, owner->identity};
}

static CH32RVTimerLease acquire(const CH32RVTimerRequest *request,
                              const CH32RVTimerOwner *owner, bool takeover)
{
    if (!request || !owner_present(owner)) {
        return invalid_lease();
    }

    uint32_t irq_state = irq_save();
    CH32RVTimerLease result = invalid_lease();
    if (request->timer != CH32RV_TIMER_ANY) {
        int index = ch32rv_timer_index(request->timer);
        if (index >= 0) {
            result = acquire_at((size_t)index, request, owner, takeover);
        }
    } else {
        /* Even takeover first tries every compatible/free timer.  It only
         * displaces the first suitable timer when no polite acquisition can
         * satisfy the request. */
        for (size_t i = 0; i < CH32RV_TIMER_COUNT && result.timer == 0; i++) {
            result = acquire_at(i, request, owner, false);
        }
        if (takeover && result.timer == 0) {
            for (size_t i = 0; i < CH32RV_TIMER_COUNT && result.timer == 0; i++) {
                result = acquire_at(i, request, owner, true);
            }
        }
    }
    irq_restore(irq_state);
    return result;
}

size_t ch32rv_timer_count(void)
{
    return CH32RV_TIMER_COUNT;
}

uint32_t ch32rv_timer_clock_hz(uint8_t timer)
{
#if CH32RV_TIMCLK1_HZ == CH32RV_TIMCLK2_HZ
    (void)timer;
    return CH32RV_TIMCLK1_HZ;
#else
    return timer < 32u && ((CH32RV_TIMER_BUS2_MASK >> timer) & 1u) ? CH32RV_TIMCLK2_HZ : CH32RV_TIMCLK1_HZ;
#endif
}

const CH32RVTimerCapability *ch32rv_timer_capability(size_t index)
{
    return index < CH32RV_TIMER_COUNT ? &ch32rv_timer_caps[index] : 0;
}

const CH32RVTimerCapability *ch32rv_timer_capability_for(uint8_t timer)
{
    int index = ch32rv_timer_index(timer);
    return index >= 0 ? &ch32rv_timer_caps[index] : 0;
}

bool ch32rv_timer_get_status(uint8_t timer, CH32RVTimerStatus *status)
{
    int index = ch32rv_timer_index(timer);
    if (index < 0 || !status) {
        return false;
    }
    uint32_t irq_state = irq_save();
    const TimerState *state = &timer_states[index];
    const CH32RVTimerCapability *cap = &ch32rv_timer_caps[index];
    status->configured = state->configured;
    status->running = (CH32RV_TIM_CTLR1(cap->register_base) &
                       CH32RV_TIM_CTLR1_CEN) != 0u;
    status->update_interrupt_owned = ch32rv_timer_update_owned((size_t)index);
    status->base_generation = state->base_generation;
    status->base = state->base;
    status->whole_owner_identity =
        state->whole_owner ? state->whole_owner->identity : 0;
    for (unsigned i = 0; i < 4; i++) {
        status->channel_owner_identity[i] =
            state->channel[i].owner ? state->channel[i].owner->identity : 0;
    }
    irq_restore(irq_state);
    return true;
}

CH32RVTimerLease ch32rv_timer_try_acquire(const CH32RVTimerRequest *request,
                                   const CH32RVTimerOwner *owner)
{
    return acquire(request, owner, false);
}

CH32RVTimerLease ch32rv_timer_takeover(const CH32RVTimerRequest *request,
                                 const CH32RVTimerOwner *owner)
{
    return acquire(request, owner, true);
}

bool ch32rv_timer_lease_valid(const CH32RVTimerLease *lease)
{
    if (!lease || !lease->owner_identity) {
        return false;
    }
    int index = ch32rv_timer_index(lease->timer);
    if (index < 0) {
        return false;
    }
    uint32_t irq_state = irq_save();
    TimerState *state = &timer_states[index];
    bool valid = state->base_generation == lease->base_generation;
    if (valid && lease->channel == CH32RV_TIMER_WHOLE) {
        valid = state->whole_owner &&
                state->whole_owner->identity == lease->owner_identity;
    } else if (valid && lease->channel >= 1u && lease->channel <= 4u) {
        const ChannelOwner *slot = &state->channel[lease->channel - 1u];
        valid = slot->owner && slot->owner->identity == lease->owner_identity &&
                slot->generation == lease->channel_generation;
    } else {
        valid = false;
    }
    irq_restore(irq_state);
    return valid;
}

void ch32rv_timer_release(CH32RVTimerLease *lease)
{
    if (!ch32rv_timer_lease_valid(lease)) {
        if (lease) {
            *lease = invalid_lease();
        }
        return;
    }
    uint32_t irq_state = irq_save();
    int index = ch32rv_timer_index(lease->timer);
    TimerState *state = &timer_states[index];
    const CH32RVTimerCapability *cap = &ch32rv_timer_caps[index];
    if (lease->channel == CH32RV_TIMER_WHOLE) {
        stop_hardware(cap);
        state->whole_owner = 0;
        ch32rv_timer_update_forget((size_t)index, 0);
        state->configured = false;
        state->base_generation = next_generation(state->base_generation);
        state->applied_generation = 0;
    } else {
        disable_channel(cap, lease->channel);
        ChannelOwner *slot = &state->channel[lease->channel - 1u];
        slot->owner = 0;
        slot->generation = next_generation(slot->generation);
        if (!any_channel_owner(state)) {
            stop_hardware(cap);
            state->configured = false;
            state->base_generation = next_generation(state->base_generation);
            state->applied_generation = 0;
        }
    }
    *lease = invalid_lease();
    irq_restore(irq_state);
}

bool ch32rv_timer_apply_base(const CH32RVTimerLease *lease)
{
    if (!ch32rv_timer_lease_valid(lease)) {
        return false;
    }
    int index = ch32rv_timer_index(lease->timer);
    const CH32RVTimerCapability *cap = &ch32rv_timer_caps[index];
    TimerState *state = &timer_states[index];
    if (state->applied_generation == state->base_generation) {
        return true;
    }
    ch32rv_clock_enable_at(cap->clock_enable_address, cap->clock_enable_mask);
    CH32RV_TIM_CTLR1(cap->register_base) = 0;
    CH32RV_TIM_PSC(cap->register_base) = state->base.prescaler;
    if (cap->counter_bits == 32u) {
        CH32RV_TIM_ATRLR32(cap->register_base) = state->base.reload;
    } else {
        CH32RV_TIM_ATRLR(cap->register_base) = (uint16_t)state->base.reload;
    }
    /* UG loads the prescaler, but with the counter stopped it does not clear
     * the count (V307, L103; 16- and 32-bit timers alike). A count left by
     * the previous owner above the new reload would run on to the counter's
     * end first: 2^32 ticks on a 32-bit timer - tone() on L103's TIM4 went
     * silent for minutes after a lower note. */
    CH32RV_TIM_SWEVGR(cap->register_base) = CH32RV_TIM_SWEVGR_UG;
    if (cap->counter_bits == 32u) {
        CH32RV_TIM_CNT32(cap->register_base) = 0;
    } else {
        CH32RV_TIM_CNT(cap->register_base) = 0;
    }
    CH32RV_TIM_INTFR(cap->register_base) = 0;
    CH32RV_TIM_CTLR1(cap->register_base) = state->base.control;
    state->applied_generation = state->base_generation;
    return true;
}

bool ch32rv_timer_start(const CH32RVTimerLease *lease)
{
    if (!ch32rv_timer_lease_valid(lease)) {
        return false;
    }
    const CH32RVTimerCapability *cap = ch32rv_timer_capability_for(lease->timer);
    CH32RV_TIM_CTLR1(cap->register_base) |= CH32RV_TIM_CTLR1_CEN;
    return true;
}

bool ch32rv_timer_stop(const CH32RVTimerLease *lease)
{
    /* A channel lease shares the counter and must not stop other channels. */
    if (!ch32rv_timer_lease_valid(lease) ||
        lease->channel != CH32RV_TIMER_WHOLE) {
        return false;
    }
    const CH32RVTimerCapability *cap = ch32rv_timer_capability_for(lease->timer);
    CH32RV_TIM_CTLR1(cap->register_base) &= (uint16_t)~CH32RV_TIM_CTLR1_CEN;
    return true;
}
