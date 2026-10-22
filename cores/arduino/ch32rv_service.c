/* The round over the attached system services (ch32rv_service.h). Its own
 * file, so it is only pulled out of the core archive when a library attaches
 * a service: delay() and main() reach ch32rv_services_run through a weak
 * reference, which does not pull an archive member by itself. */
#include "ch32rv_service.h"

#include <stddef.h>

static ch32rv_service_t *head;
static uint8_t running;

void ch32rv_service_attach(ch32rv_service_t *s)
{
    if (!s || s->attached) {
        return;
    }
    s->next = NULL;
    s->attached = 1u;
    ch32rv_service_t **at = &head;
    while (*at) {
        at = &(*at)->next;
    }
    *at = s;
}

/* The node keeps its next pointer, so a round that has already stepped past
 * it (or is about to) still reaches the rest of the list. */
void ch32rv_service_detach(ch32rv_service_t *s)
{
    if (!s || !s->attached) {
        return;
    }
    s->attached = 0u;
    for (ch32rv_service_t **at = &head; *at; at = &(*at)->next) {
        if (*at == s) {
            *at = s->next;
            return;
        }
    }
}

void ch32rv_services_run(void)
{
    if (running) {
        return;
    }
    running = 1u;
    for (ch32rv_service_t *s = head; s;) {
        ch32rv_service_t *const next = s->next;
        if (s->attached) {
            s->run();
        }
        s = next;
    }
    running = 0u;
}
