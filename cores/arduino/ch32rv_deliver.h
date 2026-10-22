/* Delivery of the sketch's notification callbacks (ADR-0016 rule 2).
 *
 * A driver's service (ch32rv_service.h) runs inside delay() and after loop();
 * the sketch's own functions must not. So a driver that has a notification
 * for the sketch - data arrived, the host set the line coding, a frame went
 * out - records it and returns, and the callback runs only when the sketch
 * calls deliverEvents():
 *
 *     void setup() { SerialUSB.onRx(gotData); }
 *     void loop()  { deliverEvents(); }   // gotData() runs here, nowhere else
 *
 * A notification not yet delivered is kept, not dropped: a flag and the
 * latest state, or the data held in the driver's buffer (the host waits).
 * Several of one kind before a delivery may arrive as one.
 *
 * The driver owns one ch32rv_delivery_t and attaches it when it begins, the
 * same intrusive-list shape as the services. Normal context only. Never
 * nested: deliverEvents() inside a callback it runs returns 0. Callbacks that
 * are not notifications - a provider the protocol waits on, such as a mass
 * storage read - are not delivered here; the driver calls them from its
 * service (ADR-0016 rule 3).
 *
 * In its own file, so a sketch that neither attaches nor calls it links
 * nothing. */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ch32rv_delivery {
    size_t (*deliver)(void);   /* run the recorded callbacks; returns how many */
    struct ch32rv_delivery *next;
    uint8_t attached;
} ch32rv_delivery_t;

#define CH32RV_DELIVERY_INIT(fn) { (fn), 0, 0 }

/* Attaching one that is attached, or detaching one that is not, does nothing.
 * Detaching from inside a delivery is allowed. */
void ch32rv_delivery_attach(ch32rv_delivery_t *d);
void ch32rv_delivery_detach(ch32rv_delivery_t *d);

/* Run every recorded notification callback once, in attach order. Returns how
 * many callbacks ran; 0 from inside one. */
size_t deliverEvents(void);

#ifdef __cplusplus
}
#endif
