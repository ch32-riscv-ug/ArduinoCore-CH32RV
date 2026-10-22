/* System services: a driver's normal-context work that has to keep running
 * whether or not the sketch calls into the driver - the USB device and host
 * stacks, the IP stack, USB PD. ADR / planning ARCH-SELECT-01 decision D-2.
 *
 * The driver owns one ch32rv_service_t (an intrusive node: there is no table,
 * so no capacity to run out of) and attaches it when it begins; delay() and
 * the return of loop() then run every attached service once, in attach order.
 * A sketch never registers anything and never calls a maintain(): begin() is
 * the whole contract.
 *
 *   - Normal context only: attach, detach and the round itself are not for
 *     ISRs (an ISR sets state or a flag; the service picks it up).
 *   - Never nested. A service that ends up in delay() - directly, or through a
 *     callback of the sketch's it runs - does not start the round again; the
 *     outer round carries on afterwards. Each service also keeps its own
 *     reentry guard, because a sketch may call into the driver from inside
 *     another service.
 *   - No timing promise. A loop() or a callback that does not return stops
 *     every service; work with a deadline belongs in the IRQ or the hardware.
 *   - Only driver code runs here. The sketch's notification callbacks are not
 *     services (decision D-3).
 *
 * ch32rv_services_run is weak: it lives beside attach() in ch32rv_service.c,
 * so a sketch whose libraries never attach links neither the list nor the
 * round, and delay() keeps a single null test. */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ch32rv_service {
    void (*run)(void);
    struct ch32rv_service *next;
    uint8_t attached;
} ch32rv_service_t;

#define CH32RV_SERVICE_INIT(fn) { (fn), 0, 0 }

/* Add the service to the round; attaching one that is already attached does
 * nothing. Detaching one that is not attached does nothing either. Detaching
 * from inside a round (a service that ends itself) is allowed: the round skips
 * it and carries on. */
void ch32rv_service_attach(ch32rv_service_t *s);
void ch32rv_service_detach(ch32rv_service_t *s);

/* One round over the attached services. delay() and main() call it. */
extern void ch32rv_services_run(void) __attribute__((weak));

#ifdef __cplusplus
}
#endif
