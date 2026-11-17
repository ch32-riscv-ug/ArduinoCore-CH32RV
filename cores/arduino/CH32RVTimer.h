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

/* The manager keeps a pointer to the owner, not a copy: the owner object
 * itself must stay valid for the lifetime of the lease (a static object, or a
 * member of the library's instance - not a local). */
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

size_t ch32rv_timer_count(void);
const CH32RVTimerCapability *ch32rv_timer_capability(size_t index);
/* The clock a timer counts, in Hz: its bus's, twice that when the CPU Clock
 * entry divides the bus (internal/ch32rv_bus_clock.h). A constant where both
 * buses' timers count HCLK. */
uint32_t ch32rv_timer_clock_hz(uint8_t timer);
const CH32RVTimerCapability *ch32rv_timer_capability_for(uint8_t timer);
bool ch32rv_timer_get_status(uint8_t timer, CH32RVTimerStatus *status);

/* Polite acquisition leaves an incompatible owner untouched.  Takeover uses
 * last-caller-wins: it quiesces and invalidates the displaced lease(s). */
CH32RVTimerLease ch32rv_timer_try_acquire(const CH32RVTimerRequest *request,
                                   const CH32RVTimerOwner *owner);
CH32RVTimerLease ch32rv_timer_takeover(const CH32RVTimerRequest *request,
                                 const CH32RVTimerOwner *owner);
bool ch32rv_timer_lease_valid(const CH32RVTimerLease *lease);
void ch32rv_timer_release(CH32RVTimerLease *lease);

/* Common operations which preserve ownership checks.  A library may use the
 * register_base in the capability for a feature this small API does not yet
 * cover, but it must still hold a valid lease while doing so. */
bool ch32rv_timer_apply_base(const CH32RVTimerLease *lease);
bool ch32rv_timer_start(const CH32RVTimerLease *lease);
bool ch32rv_timer_stop(const CH32RVTimerLease *lease);
bool ch32rv_timer_attach_update_interrupt(const CH32RVTimerLease *lease,
                                    CH32RVTimerCallback callback,
                                    void *context);
void ch32rv_timer_detach_update_interrupt(const CH32RVTimerLease *lease);

/* The capture / compare interrupt of a timer held whole: callback(context)
 * runs on every interrupt of the timer's capture / compare channels, from the
 * timer's own capture / compare vector (advanced timers) or the vector it
 * shares with the update (the others). The callback reads and clears the
 * channels' flags in INTFR itself; which channels interrupt is the CCxIE bits
 * of DMAINTENR, which the owner sets. A lease that is taken over or released
 * loses its callback, as with the update interrupt. Where the vector is
 * shared, this callback runs first on each interrupt and may take the update
 * flag itself (an owner extending its counter past its width attaches both
 * with one function). */
bool ch32rv_timer_attach_channel_interrupt(const CH32RVTimerLease *lease,
                                           CH32RVTimerCallback callback,
                                           void *context);
void ch32rv_timer_detach_channel_interrupt(const CH32RVTimerLease *lease);

/* ---- Pads ------------------------------------------------------------------
 * Every pad a timer reaches, on every route the chip offers (the variant's
 * CH32RV_TIMER_PADS, from device-data): a capture / compare channel (CHx), its
 * complementary output (CHxN) and the break input. A route is how the pad is
 * connected: on most families one AFIO field moves all of a timer's pads at
 * once (the route is that field's value, 0 the reset default); on the
 * per-pin families (V205, X305, X315, H41x) each pad picks its own alternate
 * function (the route is that number). A pad can be on several timers and
 * routes; ch32rv_timer_pad_find() gives them in the order analogWrite() tries
 * them, its default PWM first. */
#define CH32RV_TIMER_PAD_CH    0u   /* a capture / compare channel */
#define CH32RV_TIMER_PAD_CHN   1u   /* a complementary output (advanced timers) */
#define CH32RV_TIMER_PAD_BKIN  2u   /* the break input (advanced timers), channel 0 */

typedef struct {
    uint8_t pin;        /* the pad (port-encoded pin number) */
    uint8_t timer;
    uint8_t channel;    /* 1..4; 0 for the break input */
    uint8_t role;       /* CH32RV_TIMER_PAD_* */
    uint8_t route;      /* the timer's remap value, or the pad's function */
} CH32RVTimerPad;

/* The n-th way (0, 1, ...) that `pin` reaches a timer in one of `roles`
 * (CH32RV_TIMER_ROLES(...) of CH32RV_TIMER_PAD_*), in the table's order. pin
 * is a pin number of the sketch's, as digitalWrite() takes. False past the
 * last, and on a pad that reaches none. */
#define CH32RV_TIMER_ROLE(role) (1u << (role))
bool ch32rv_timer_pad_find(uint32_t pin, uint8_t roles, uint8_t n,
                           CH32RVTimerPad *out);
/* The route the timer's AFIO field is set to now; 0 on the per-pin families,
 * where a timer has no route of its own. */
uint8_t ch32rv_timer_route(uint8_t timer);
/* Connect the pad to its timer: the timer's AFIO field set to the pad's route
 * (moving the timer's other pads with it), or the pad's alternate function.
 * output: the pad becomes an alternate-function push-pull output; else it is
 * left in the input mode pinMode() gave it. Hold a lease on the timer. */
void ch32rv_timer_pad_connect(const CH32RVTimerPad *pad, bool output);
/* Whether two pads of a timer can be connected at once: the same timer, and
 * the same route where one AFIO field moves all of its pads. */
bool ch32rv_timer_pads_together(const CH32RVTimerPad *a, const CH32RVTimerPad *b);

#ifdef __cplusplus
}
#endif
