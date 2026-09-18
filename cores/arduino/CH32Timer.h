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

#define CH32_TIMER_ANY     0u
#define CH32_TIMER_WHOLE   0u

typedef enum {
    CH32_TIMER_KIND_UNKNOWN = 0,
    CH32_TIMER_KIND_ADVANCED = 1,
    CH32_TIMER_KIND_GENERAL = 2,
    CH32_TIMER_KIND_STREAMLINED = 3,
} CH32TimerKind;

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
} CH32TimerCapability;

/* The values written to PSC, ATRLR and CTLR1 (apart from CEN).  Two channel
 * leases may share a timer only when all three fields match exactly. */
typedef struct {
    uint16_t prescaler;
    uint32_t reload;
    uint16_t control;
} CH32TimerBaseConfig;

typedef void (*CH32TimerQuiesce)(void *context, uint8_t timer,
                                 uint8_t channel);
typedef void (*CH32TimerCallback)(void *context);

typedef struct {
    /* Must be non-null and stable for the lifetime of a lease.  A library
     * normally uses the address of one static object or of its instance. */
    const void *identity;
    CH32TimerQuiesce quiesce;
    void *context;
} CH32TimerOwner;

typedef struct {
    uint8_t timer;       /* CH32_TIMER_ANY asks the manager to choose */
    uint8_t channel;     /* CH32_TIMER_WHOLE or 1..capability.channels */
    CH32TimerBaseConfig base;
} CH32TimerRequest;

typedef struct {
    uint8_t timer;
    uint8_t channel;
    uint16_t base_generation;
    uint16_t channel_generation;
    const void *owner_identity;
} CH32TimerLease;

typedef struct {
    bool configured;
    bool running;
    bool update_interrupt_owned;
    uint16_t base_generation;
    CH32TimerBaseConfig base;
    const void *whole_owner_identity;
    const void *channel_owner_identity[4];
} CH32TimerStatus;

size_t ch32TimerCount(void);
const CH32TimerCapability *ch32TimerCapability(size_t index);
const CH32TimerCapability *ch32TimerCapabilityFor(uint8_t timer);
bool ch32TimerGetStatus(uint8_t timer, CH32TimerStatus *status);

/* Polite acquisition leaves an incompatible owner untouched.  Takeover uses
 * last-caller-wins: it quiesces and invalidates the displaced lease(s). */
CH32TimerLease ch32TimerTryAcquire(const CH32TimerRequest *request,
                                   const CH32TimerOwner *owner);
CH32TimerLease ch32TimerTakeover(const CH32TimerRequest *request,
                                 const CH32TimerOwner *owner);
bool ch32TimerLeaseValid(const CH32TimerLease *lease);
void ch32TimerRelease(CH32TimerLease *lease);

/* Common operations which preserve ownership checks.  A library may use the
 * register_base in the capability for a feature this small API does not yet
 * cover, but it must still hold a valid lease while doing so. */
bool ch32TimerApplyBase(const CH32TimerLease *lease);
bool ch32TimerStart(const CH32TimerLease *lease);
bool ch32TimerStop(const CH32TimerLease *lease);
bool ch32TimerAttachUpdateInterrupt(const CH32TimerLease *lease,
                                    CH32TimerCallback callback,
                                    void *context);
void ch32TimerDetachUpdateInterrupt(const CH32TimerLease *lease);

#ifdef __cplusplus
}
#endif
