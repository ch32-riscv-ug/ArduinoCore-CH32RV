/* Public CH32 timer resource API.
 *
 * Libraries use this API to participate in the same TIM ownership rules as
 * analogWrite(), tone() and Servo.  It is deliberately a resource API, not a
 * port of another core's HardwareTimer class.  A channel value of zero means
 * exclusive ownership of the whole timer; 1..4 names a capture/compare
 * channel whose base counter may be shared when the configurations match.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CH32RV_TIMER_ANY     0u
#define CH32RV_TIMER_WHOLE   0u

typedef enum {
    CH32RV_TIMER_KIND_UNKNOWN = 0,
    CH32RV_TIMER_KIND_ADVANCED = 1,
    CH32RV_TIMER_KIND_GENERAL = 2,
    CH32RV_TIMER_KIND_STREAMLINED = 3,
} CH32RVTimerKind;

typedef struct {
    uint8_t number;
    uint8_t kind;
    uint8_t counter_bits;
    uint8_t channels;
    bool complementary_outputs;
    uint32_t register_base;
    uint32_t clock_enable_address;
    uint32_t clock_enable_mask;
    uint32_t update_irq;
} CH32RVTimerCapability;

/* The values written to PSC, ATRLR and CTLR1 (apart from CEN).  Two channel
 * leases may share a timer only when all three fields match exactly. */
typedef struct {
    uint16_t prescaler;
    uint32_t reload;
    uint16_t control;
} CH32RVTimerBaseConfig;

typedef void (*CH32RVTimerQuiesce)(void *context, uint8_t timer,
                                 uint8_t channel);
typedef void (*CH32RVTimerCallback)(void *context);

typedef struct {
    /* Must be non-null and stable for the lifetime of a lease.  A library
     * normally uses the address of one static object or of its instance. */
    const void *identity;
    CH32RVTimerQuiesce quiesce;
    void *context;
} CH32RVTimerOwner;

typedef struct {
    uint8_t timer;       /* CH32RV_TIMER_ANY asks the manager to choose */
    uint8_t channel;     /* CH32RV_TIMER_WHOLE or 1..capability.channels */
    CH32RVTimerBaseConfig base;
} CH32RVTimerRequest;

typedef struct {
    uint8_t timer;
    uint8_t channel;
    uint16_t base_generation;
    uint16_t channel_generation;
    const void *owner_identity;
} CH32RVTimerLease;

typedef struct {
    bool configured;
    bool running;
    bool update_interrupt_owned;
    uint16_t base_generation;
    CH32RVTimerBaseConfig base;
    const void *whole_owner_identity;
    const void *channel_owner_identity[4];
} CH32RVTimerStatus;

size_t ch32TimerCount(void);
const CH32RVTimerCapability *ch32TimerCapability(size_t index);
const CH32RVTimerCapability *ch32TimerCapabilityFor(uint8_t timer);
bool ch32TimerGetStatus(uint8_t timer, CH32RVTimerStatus *status);

/* Polite acquisition leaves an incompatible owner untouched.  Takeover uses
 * last-caller-wins: it quiesces and invalidates the displaced lease(s). */
CH32RVTimerLease ch32TimerTryAcquire(const CH32RVTimerRequest *request,
                                   const CH32RVTimerOwner *owner);
CH32RVTimerLease ch32TimerTakeover(const CH32RVTimerRequest *request,
                                 const CH32RVTimerOwner *owner);
bool ch32TimerLeaseValid(const CH32RVTimerLease *lease);
void ch32TimerRelease(CH32RVTimerLease *lease);

/* Common operations which preserve ownership checks.  A library may use the
 * register_base in the capability for a feature this small API does not yet
 * cover, but it must still hold a valid lease while doing so. */
bool ch32TimerApplyBase(const CH32RVTimerLease *lease);
bool ch32TimerStart(const CH32RVTimerLease *lease);
bool ch32TimerStop(const CH32RVTimerLease *lease);
bool ch32TimerAttachUpdateInterrupt(const CH32RVTimerLease *lease,
                                    CH32RVTimerCallback callback,
                                    void *context);
void ch32TimerDetachUpdateInterrupt(const CH32RVTimerLease *lease);

#ifdef __cplusplus
}
#endif
