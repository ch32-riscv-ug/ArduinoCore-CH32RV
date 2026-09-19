/* Shared TIM ownership and update-IRQ dispatch.
 *
 * There is no heap and no hidden lock policy.  Arduino's void standard APIs
 * use takeover; libraries that can report a busy resource use tryAcquire.
 */
#include "Arduino.h"
#include "CH32Timer.h"
#include "ch32_registers.h"

#include <string.h>

typedef struct {
    CH32TimerOwner owner;
    uint16_t generation;
} ChannelOwner;

typedef struct {
    CH32TimerBaseConfig base;
    CH32TimerOwner whole_owner;
    ChannelOwner channel[4];
    CH32TimerCallback update_callback;
    void *update_context;
    const void *update_owner;
    uint16_t base_generation;
    uint16_t applied_generation;
    bool configured;
} TimerState;

#define TIMER_CAP(number, kind, bits, channels, complementary, base, clkaddr, \
                  clkmask, irq, handler)                                      \
    { (number), (kind), (bits), (channels), (complementary) != 0, (base),      \
      (clkaddr), (clkmask), (irq) },
static const CH32TimerCapability timer_capabilities[] = {
    CH32_TIMER_TABLE(TIMER_CAP)
};
#undef TIMER_CAP

static TimerState timer_states[CH32_TIMER_COUNT];

static uint32_t irq_save(void)
{
    uint32_t old;
    __asm__ volatile ("csrrci %0, mstatus, 8" : "=r"(old) :: "memory");
    return old;
}

static void irq_restore(uint32_t old)
{
    if (old & 8u) {
        __asm__ volatile ("csrsi mstatus, 8" ::: "memory");
    }
}

static uint16_t next_generation(uint16_t value)
{
    value++;
    return value ? value : 1u;
}

static int capability_index(uint8_t timer)
{
    for (size_t i = 0; i < CH32_TIMER_COUNT; i++) {
        if (timer_capabilities[i].number == timer) {
            return (int)i;
        }
    }
    return -1;
}

static bool owner_present(const CH32TimerOwner *owner)
{
    return owner && owner->identity;
}

static bool same_base(const CH32TimerBaseConfig *a,
                      const CH32TimerBaseConfig *b)
{
    return a->prescaler == b->prescaler && a->reload == b->reload &&
           a->control == b->control;
}

static bool any_channel_owner(const TimerState *state)
{
    for (unsigned i = 0; i < 4; i++) {
        if (state->channel[i].owner.identity) {
            return true;
        }
    }
    return false;
}

static void stop_hardware(const CH32TimerCapability *cap)
{
    CH32_TIM_CTLR1(cap->register_base) = 0;
    CH32_TIM_DMAINTENR(cap->register_base) = 0;
    CH32_TIM_CCER(cap->register_base) = 0;
    CH32_TIM_INTFR(cap->register_base) = 0;
    ch32_irq_disable(cap->update_irq);
}

static void disable_channel(const CH32TimerCapability *cap, uint8_t channel)
{
    const uint16_t enable = (uint16_t)(1u << ((channel - 1u) * 4u));
    const uint16_t interrupt = (uint16_t)(1u << channel);
    CH32_TIM_CCER(cap->register_base) &= (uint16_t)~enable;
    CH32_TIM_DMAINTENR(cap->register_base) &= (uint16_t)~interrupt;
    CH32_TIM_INTFR(cap->register_base) = (uint16_t)~interrupt;
}

static void call_quiesce(const CH32TimerOwner *owner, uint8_t timer,
                         uint8_t channel)
{
    if (owner->identity && owner->quiesce) {
        owner->quiesce(owner->context, timer, channel);
    }
}

static void invalidate_channel(size_t index, uint8_t channel)
{
    TimerState *state = &timer_states[index];
    const CH32TimerCapability *cap = &timer_capabilities[index];
    ChannelOwner old = state->channel[channel - 1u];

    disable_channel(cap, channel);
    memset(&state->channel[channel - 1u].owner, 0, sizeof(CH32TimerOwner));
    state->channel[channel - 1u].generation =
        next_generation(state->channel[channel - 1u].generation);
    if (state->update_owner == old.owner.identity) {
        state->update_callback = 0;
        state->update_context = 0;
        state->update_owner = 0;
    }
    call_quiesce(&old.owner, cap->number, channel);
}

static void invalidate_all(size_t index)
{
    TimerState *state = &timer_states[index];
    const CH32TimerCapability *cap = &timer_capabilities[index];
    CH32TimerOwner owners[5];
    unsigned count = 0;

    stop_hardware(cap);
    if (state->whole_owner.identity) {
        owners[count] = state->whole_owner;
        count++;
    }
    for (unsigned i = 0; i < 4; i++) {
        CH32TimerOwner owner = state->channel[i].owner;
        if (!owner.identity) {
            continue;
        }
        bool duplicate = false;
        for (unsigned j = 0; j < count; j++) {
            if (owners[j].identity == owner.identity) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            owners[count] = owner;
            count++;
        }
    }

    memset(&state->whole_owner, 0, sizeof(state->whole_owner));
    for (unsigned i = 0; i < 4; i++) {
        memset(&state->channel[i].owner, 0, sizeof(CH32TimerOwner));
        state->channel[i].generation =
            next_generation(state->channel[i].generation);
    }
    state->update_callback = 0;
    state->update_context = 0;
    state->update_owner = 0;
    state->configured = false;
    state->base_generation = next_generation(state->base_generation);
    state->applied_generation = 0;

    for (unsigned i = 0; i < count; i++) {
        call_quiesce(&owners[i], cap->number, CH32_TIMER_WHOLE);
    }
}

static CH32TimerLease invalid_lease(void)
{
    CH32TimerLease lease = {0};
    return lease;
}

static CH32TimerLease acquire_at(size_t index, const CH32TimerRequest *request,
                                 const CH32TimerOwner *owner, bool takeover)
{
    const CH32TimerCapability *cap = &timer_capabilities[index];
    TimerState *state = &timer_states[index];
    const uint8_t channel = request->channel;

    if (channel > cap->channels || (channel == CH32_TIMER_WHOLE &&
                                    cap->update_irq == 0u)) {
        return invalid_lease();
    }

    const bool occupied = state->whole_owner.identity || any_channel_owner(state);
    const bool base_compatible = !state->configured ||
                                 same_base(&state->base, &request->base);

    if (channel == CH32_TIMER_WHOLE) {
        if (occupied && state->whole_owner.identity == owner->identity &&
            !any_channel_owner(state) && base_compatible) {
            state->whole_owner = *owner;
        } else if (occupied) {
            if (!takeover) {
                return invalid_lease();
            }
            invalidate_all(index);
        }
        if (!state->whole_owner.identity) {
            state->base = request->base;
            state->configured = true;
            state->base_generation = next_generation(state->base_generation);
            state->applied_generation = 0;
            state->whole_owner = *owner;
        }
        return (CH32TimerLease){cap->number, CH32_TIMER_WHOLE,
                                state->base_generation, 0, owner->identity};
    }

    if (state->whole_owner.identity || (occupied && !base_compatible)) {
        if (!takeover) {
            return invalid_lease();
        }
        invalidate_all(index);
    }

    ChannelOwner *slot = &state->channel[channel - 1u];
    if (slot->owner.identity && slot->owner.identity != owner->identity) {
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
    if (!slot->owner.identity) {
        slot->generation = next_generation(slot->generation);
    }
    slot->owner = *owner;
    return (CH32TimerLease){cap->number, channel, state->base_generation,
                            slot->generation, owner->identity};
}

static CH32TimerLease acquire(const CH32TimerRequest *request,
                              const CH32TimerOwner *owner, bool takeover)
{
    if (!request || !owner_present(owner)) {
        return invalid_lease();
    }

    uint32_t irq_state = irq_save();
    CH32TimerLease result = invalid_lease();
    if (request->timer != CH32_TIMER_ANY) {
        int index = capability_index(request->timer);
        if (index >= 0) {
            result = acquire_at((size_t)index, request, owner, takeover);
        }
    } else {
        /* Even takeover first tries every compatible/free timer.  It only
         * displaces the first suitable timer when no polite acquisition can
         * satisfy the request. */
        for (size_t i = 0; i < CH32_TIMER_COUNT && result.timer == 0; i++) {
            result = acquire_at(i, request, owner, false);
        }
        if (takeover && result.timer == 0) {
            for (size_t i = 0; i < CH32_TIMER_COUNT && result.timer == 0; i++) {
                result = acquire_at(i, request, owner, true);
            }
        }
    }
    irq_restore(irq_state);
    return result;
}

size_t ch32TimerCount(void)
{
    return CH32_TIMER_COUNT;
}

const CH32TimerCapability *ch32TimerCapability(size_t index)
{
    return index < CH32_TIMER_COUNT ? &timer_capabilities[index] : 0;
}

const CH32TimerCapability *ch32TimerCapabilityFor(uint8_t timer)
{
    int index = capability_index(timer);
    return index >= 0 ? &timer_capabilities[index] : 0;
}

bool ch32TimerGetStatus(uint8_t timer, CH32TimerStatus *status)
{
    int index = capability_index(timer);
    if (index < 0 || !status) {
        return false;
    }
    uint32_t irq_state = irq_save();
    const TimerState *state = &timer_states[index];
    const CH32TimerCapability *cap = &timer_capabilities[index];
    status->configured = state->configured;
    status->running = (CH32_TIM_CTLR1(cap->register_base) &
                       CH32_TIM_CTLR1_CEN) != 0u;
    status->update_interrupt_owned = state->update_owner != 0;
    status->base_generation = state->base_generation;
    status->base = state->base;
    status->whole_owner_identity = state->whole_owner.identity;
    for (unsigned i = 0; i < 4; i++) {
        status->channel_owner_identity[i] =
            state->channel[i].owner.identity;
    }
    irq_restore(irq_state);
    return true;
}

CH32TimerLease ch32TimerTryAcquire(const CH32TimerRequest *request,
                                   const CH32TimerOwner *owner)
{
    return acquire(request, owner, false);
}

CH32TimerLease ch32TimerTakeover(const CH32TimerRequest *request,
                                 const CH32TimerOwner *owner)
{
    return acquire(request, owner, true);
}

bool ch32TimerLeaseValid(const CH32TimerLease *lease)
{
    if (!lease || !lease->owner_identity) {
        return false;
    }
    int index = capability_index(lease->timer);
    if (index < 0) {
        return false;
    }
    uint32_t irq_state = irq_save();
    TimerState *state = &timer_states[index];
    bool valid = state->base_generation == lease->base_generation;
    if (valid && lease->channel == CH32_TIMER_WHOLE) {
        valid = state->whole_owner.identity == lease->owner_identity;
    } else if (valid && lease->channel >= 1u && lease->channel <= 4u) {
        const ChannelOwner *slot = &state->channel[lease->channel - 1u];
        valid = slot->owner.identity == lease->owner_identity &&
                slot->generation == lease->channel_generation;
    } else {
        valid = false;
    }
    irq_restore(irq_state);
    return valid;
}

void ch32TimerRelease(CH32TimerLease *lease)
{
    if (!ch32TimerLeaseValid(lease)) {
        if (lease) {
            *lease = invalid_lease();
        }
        return;
    }
    uint32_t irq_state = irq_save();
    int index = capability_index(lease->timer);
    TimerState *state = &timer_states[index];
    const CH32TimerCapability *cap = &timer_capabilities[index];
    if (lease->channel == CH32_TIMER_WHOLE) {
        stop_hardware(cap);
        memset(&state->whole_owner, 0, sizeof(state->whole_owner));
        state->update_callback = 0;
        state->update_context = 0;
        state->update_owner = 0;
        state->configured = false;
        state->base_generation = next_generation(state->base_generation);
        state->applied_generation = 0;
    } else {
        disable_channel(cap, lease->channel);
        ChannelOwner *slot = &state->channel[lease->channel - 1u];
        memset(&slot->owner, 0, sizeof(slot->owner));
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

bool ch32TimerApplyBase(const CH32TimerLease *lease)
{
    if (!ch32TimerLeaseValid(lease)) {
        return false;
    }
    int index = capability_index(lease->timer);
    const CH32TimerCapability *cap = &timer_capabilities[index];
    TimerState *state = &timer_states[index];
    if (state->applied_generation == state->base_generation) {
        return true;
    }
    ch32_clock_enable_at(cap->clock_enable_address, cap->clock_enable_mask);
    CH32_TIM_CTLR1(cap->register_base) = 0;
    CH32_TIM_PSC(cap->register_base) = state->base.prescaler;
    if (cap->counter_bits == 32u) {
        CH32_TIM_ATRLR32(cap->register_base) = state->base.reload;
    } else {
        CH32_TIM_ATRLR(cap->register_base) = (uint16_t)state->base.reload;
    }
    CH32_TIM_SWEVGR(cap->register_base) = CH32_TIM_SWEVGR_UG;
    CH32_TIM_INTFR(cap->register_base) = 0;
    CH32_TIM_CTLR1(cap->register_base) = state->base.control;
    state->applied_generation = state->base_generation;
    return true;
}

bool ch32TimerStart(const CH32TimerLease *lease)
{
    if (!ch32TimerLeaseValid(lease)) {
        return false;
    }
    const CH32TimerCapability *cap = ch32TimerCapabilityFor(lease->timer);
    CH32_TIM_CTLR1(cap->register_base) |= CH32_TIM_CTLR1_CEN;
    return true;
}

bool ch32TimerStop(const CH32TimerLease *lease)
{
    /* A channel lease shares the counter and must not stop other channels. */
    if (!ch32TimerLeaseValid(lease) ||
        lease->channel != CH32_TIMER_WHOLE) {
        return false;
    }
    const CH32TimerCapability *cap = ch32TimerCapabilityFor(lease->timer);
    CH32_TIM_CTLR1(cap->register_base) &= (uint16_t)~CH32_TIM_CTLR1_CEN;
    return true;
}

bool ch32TimerAttachUpdateInterrupt(const CH32TimerLease *lease,
                                    CH32TimerCallback callback, void *context)
{
    if (!callback || !ch32TimerLeaseValid(lease) ||
        lease->channel != CH32_TIMER_WHOLE) {
        return false;
    }
    int index = capability_index(lease->timer);
    uint32_t irq_state = irq_save();
    TimerState *state = &timer_states[index];
    if (state->update_owner && state->update_owner != lease->owner_identity) {
        irq_restore(irq_state);
        return false;
    }
    state->update_callback = callback;
    state->update_context = context;
    state->update_owner = lease->owner_identity;
    const CH32TimerCapability *cap = &timer_capabilities[index];
    /* Do not deliver an update event left pending before this owner attached. */
    CH32_TIM_INTFR(cap->register_base) = (uint16_t)~CH32_TIM_INT_UIE;
    CH32_TIM_DMAINTENR(cap->register_base) |= CH32_TIM_INT_UIE;
    ch32_irq_enable(cap->update_irq);
    irq_restore(irq_state);
    return true;
}

void ch32TimerDetachUpdateInterrupt(const CH32TimerLease *lease)
{
    if (!ch32TimerLeaseValid(lease)) {
        return;
    }
    int index = capability_index(lease->timer);
    uint32_t irq_state = irq_save();
    TimerState *state = &timer_states[index];
    if (state->update_owner == lease->owner_identity) {
        const CH32TimerCapability *cap = &timer_capabilities[index];
        CH32_TIM_DMAINTENR(cap->register_base) &= (uint16_t)~CH32_TIM_INT_UIE;
        ch32_irq_disable(cap->update_irq);
        state->update_callback = 0;
        state->update_context = 0;
        state->update_owner = 0;
    }
    irq_restore(irq_state);
}

static void timer_irq_dispatch(uint8_t timer)
{
    int index = capability_index(timer);
    if (index < 0) {
        return;
    }
    const CH32TimerCapability *cap = &timer_capabilities[index];
    if ((CH32_TIM_INTFR(cap->register_base) & CH32_TIM_INT_UIE) == 0u) {
        return;
    }
    CH32_TIM_INTFR(cap->register_base) = (uint16_t)~CH32_TIM_INT_UIE;
    CH32TimerCallback callback = timer_states[index].update_callback;
    if (callback) {
        callback(timer_states[index].update_context);
    }
}

#define TIMER_HANDLER(number, kind, bits, channels, complementary, base,       \
                      clkaddr, clkmask, irq, handler)                           \
    __attribute__((interrupt)) void handler(void)                               \
    {                                                                            \
        timer_irq_dispatch(number);                                               \
    }
CH32_TIMER_TABLE(TIMER_HANDLER)
#undef TIMER_HANDLER
