/* The delivery round (ch32rv_deliver.h). Its own file, so it is only pulled
 * out of the core archive when a library attaches or the sketch calls
 * deliverEvents(). */
#include "ch32rv_deliver.h"

static ch32rv_delivery_t *head;
static uint8_t running;

void ch32rv_delivery_attach(ch32rv_delivery_t *d)
{
    if (!d || d->attached) {
        return;
    }
    d->next = NULL;
    d->attached = 1u;
    ch32rv_delivery_t **at = &head;
    while (*at) {
        at = &(*at)->next;
    }
    *at = d;
}

/* The node keeps its next pointer, so a delivery that has stepped past it
 * still reaches the rest of the list. */
void ch32rv_delivery_detach(ch32rv_delivery_t *d)
{
    if (!d || !d->attached) {
        return;
    }
    d->attached = 0u;
    for (ch32rv_delivery_t **at = &head; *at; at = &(*at)->next) {
        if (*at == d) {
            *at = d->next;
            return;
        }
    }
}

size_t deliverEvents(void)
{
    if (running) {
        return 0;
    }
    running = 1u;
    size_t ran = 0;
    for (ch32rv_delivery_t *d = head; d;) {
        ch32rv_delivery_t *const next = d->next;
        if (d->attached) {
            ran += d->deliver();
        }
        d = next;
    }
    running = 0u;
    return ran;
}
